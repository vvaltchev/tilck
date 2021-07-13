/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/mod_console.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>
#include <tilck/common/color_defs.h>
#include <tilck/common/atomics.h>

#include <tilck/kernel/sched.h>
#include <tilck/kernel/interrupts.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/tty.h>
#include <tilck/kernel/datetime.h>

#define PRINTK_BUF_SZ                         224
#define PRINTK_PREFIXBUF_SZ                   32

#ifdef BITS32
   #define PRINTK_SAFE_STACK_SPACE         1536
#else
   #define PRINTK_SAFE_STACK_SPACE         2048   /* TODO: check this */
#endif



#define PRINTK_COLOR                          COLOR_GREEN
#define PRINTK_RINGBUF_FLUSH_COLOR            COLOR_CYAN
#define PRINTK_NOSPACE_IN_RBUF_FLUSH_COLOR    COLOR_MAGENTA
#define PRINTK_PANIC_COLOR                    COLOR_RED

struct ringbuf_stat {

   union {

      struct {
         u32 read_pos              : 14;
         u32 write_pos             : 14;
         u32 full                  :  1;
         u32 first_printk          :  1;
         u32 newline               :  1;
         u32 unused0               :  1;
      };

      ATOMIC(u32) raw;
      u32 __raw;
   };
};

#if TINY_KERNEL
   static char printk_rbuf[2 * KB];
#else
   static char printk_rbuf[8 * KB];
#endif

static volatile struct ringbuf_stat printk_rbuf_stat =
{
   .newline = 1
};

bool __in_printk;

/*
 * NOTE: the ring buf cannot be larger than 16K elems because of the size of the
 * read_pos and write_pos bit-fields.
 */
STATIC_ASSERT(sizeof(printk_rbuf) <= 16 * KB);

static void
printk_direct_flush_no_tty(const char *buf, size_t size, u8 color)
{
   /*
    * tty has not been initialized yet, therefore we have to translate here
    * \n to \r\n, by writing character by character to term.
    */

   for (u32 i = 0; i < size; i++) {

      if (buf[i] == '\n')
         term_write("\r", 1, color);

      term_write(&buf[i], 1, color);
   }
}

static void
printk_direct_flush(const char *buf, size_t size, u8 color)
{
   if (!size)
      return;

   __in_printk = true;
   {
      if (LIKELY(get_curr_tty() != NULL)) {

         /* tty has been initialized and set a term write filter func */

         if (UNLIKELY(in_kernel_shutdown()))
            tty_write_on_all_ttys(buf, size);
         else if (KRN_PRINTK_ON_CURR_TTY || !get_curr_process_tty())
            term_write(buf, size, color);
         else
            tty_curr_proc_write(buf, size);

      } else {

         printk_direct_flush_no_tty(buf, size, color);
      }
   }
   __in_printk = false;
   return;
}

static ALWAYS_INLINE u32
printk_calc_used(const struct ringbuf_stat *cs)
{
   u32 used = (cs->write_pos - cs->read_pos) % sizeof(printk_rbuf);

   if (!used)
      used = cs->full ? sizeof(printk_rbuf) : 0;

   return used;
}

static void
__printk_flush_ringbuf(char *tmpbuf, u32 buf_size)
{
   struct ringbuf_stat cs, ns;
   u32 used, to_read = 0;

   while (true) {

      do {
         cs = printk_rbuf_stat;
         ns = printk_rbuf_stat;
         used = printk_calc_used(&cs);

         /* We can read at most 'buf_size' bytes at a time */
         to_read = UNSAFE_MIN(buf_size, used);

         /* And copy them to our minibuf */
         for (u32 i = 0; i < to_read; i++)
            tmpbuf[i] = printk_rbuf[(cs.read_pos + i) % sizeof(printk_rbuf)];

         /* Increase read_pos and decrease used */
         ns.read_pos = (ns.read_pos + to_read) % sizeof(printk_rbuf);

         if (!to_read)
            ns.first_printk = 0;

         /* Repeat that until we were able to do that atomically */

      } while (!atomic_cas_weak(&printk_rbuf_stat.raw,
                                &cs.__raw,
                                ns.__raw,
                                mo_relaxed,
                                mo_relaxed));

      /* Note: we checked that `first_printk` in `cs` was unset! */
      if (!to_read)
         break;

      printk_direct_flush(tmpbuf, to_read, PRINTK_RINGBUF_FLUSH_COLOR);
   }
}

void
printk_flush_ringbuf(void)
{
   char minibuf[80];
   __printk_flush_ringbuf(minibuf, sizeof(minibuf));
}

static void printk_append_to_ringbuf(const char *buf, size_t size)
{
   static const char err_msg[] = "{_DROPPED_}\n";
   struct ringbuf_stat cs, ns;
   u32 used;

   if (!size)
      return;

   do {
      cs = printk_rbuf_stat;
      ns = printk_rbuf_stat;
      used = printk_calc_used(&cs);

      if (used + size >= sizeof(printk_rbuf)) {

         /* Corner case: the ring buffer is full */

         if (term_is_initialized()) {
            printk_direct_flush(buf, size, PRINTK_NOSPACE_IN_RBUF_FLUSH_COLOR);
            return;
         }

         if (buf != err_msg && used < sizeof(printk_rbuf) - 1) {
            size = MIN(sizeof(printk_rbuf) - used - 1, sizeof(err_msg));
            printk_append_to_ringbuf(err_msg, size);
         }

         return;
      }

      ns.write_pos = (ns.write_pos + size) % sizeof(printk_rbuf);

      if (ns.write_pos == ns.read_pos)
         ns.full = 1;

   } while (!atomic_cas_weak(&printk_rbuf_stat.raw,
                             &cs.__raw,
                             ns.__raw,
                             mo_relaxed,
                             mo_relaxed));

   // Now we have some allocated space in the ringbuf

   for (u32 i = 0; i < size; i++)
      printk_rbuf[(cs.write_pos + i) % sizeof(printk_rbuf)] = buf[i];
}

/*
 * Sets atomically first_printk=1 and returns the old ringbuf_stat.
 */
static struct ringbuf_stat
try_set_first_printk_on_stack(bool newline)
{
   struct ringbuf_stat cs, ns;

   do {
      cs = printk_rbuf_stat;
      ns = printk_rbuf_stat;
      ns.first_printk = 1;
      ns.newline = newline;
   } while (!atomic_cas_weak(&printk_rbuf_stat.raw,
                             &cs.__raw,
                             ns.__raw,
                             mo_relaxed,
                             mo_relaxed));

   return cs;
}

static void
restore_first_printk_value(void)
{
   struct ringbuf_stat cs, ns;

   do {
      cs = printk_rbuf_stat;
      ns = printk_rbuf_stat;
      ns.first_printk = 0;
   } while (!atomic_cas_weak(&printk_rbuf_stat.raw,
                             &cs.__raw,
                             ns.__raw,
                             mo_relaxed,
                             mo_relaxed));
}

STATIC int
vsnprintk_with_truc_suffix(char *buf, u32 bufsz, const char *fmt, va_list args)
{
   static const char truncated_str[] = "[...]";

   int written = vsnprintk(buf, bufsz, fmt, args);

   if (written == (int)bufsz) {

      /*
       * Corner case: the buffer is completely full and the final \0 has been
       * included in 'written'.
       */

      memcpy(buf + bufsz - sizeof(truncated_str),
             truncated_str,
             sizeof(truncated_str));

      written--;
   }

   return written;
}

static void
__tilck_vprintk(char *prefixbuf,
                char *buf,
                u32 bufsz,
                u32 flags,
                const char *fmt,
                va_list args)
{
   bool prefix = !in_panic();
   bool has_newline = false;
   struct ringbuf_stat old;
   int written, prefix_sz = 0;

   if (fmt[0] == PRINTK_CTRL_CHAR) {

      char cmd = fmt[1];
      fmt += 4;

      if (NO_PREFIX[0]) {

         /* NO_PREFIX is not empty, so we're not in unit tests */

         if (cmd == NO_PREFIX[1])
            prefix = false;
      }
   }

   if (flags & PRINTK_FL_NO_PREFIX)
      prefix = false;

   written = vsnprintk_with_truc_suffix(buf, bufsz, fmt, args);

   for (int i = 0; i < written; i++) {
      if (buf[i] == '\n') {
         has_newline = true;
         break;
      }
   }

   old = try_set_first_printk_on_stack(has_newline);

   if (prefix && old.newline) {

      const u64 systime = get_sys_time();

      prefix_sz = snprintk(
         prefixbuf, PRINTK_PREFIXBUF_SZ, "[%5u.%03u] %s",
         (u32)(systime / TS_SCALE),
         (u32)((systime % TS_SCALE) / (TS_SCALE / 1000)),
         bufsz < PRINTK_BUF_SZ ? "[LOWSS] " : ""
      );

   } else {

      prefix = false;
   }


   if (!term_is_initialized()) {
      printk_append_to_ringbuf(prefixbuf, (size_t) prefix_sz);
      printk_append_to_ringbuf(buf, (size_t) written);
      restore_first_printk_value();
      return;
   }

   if (in_panic()) {
      printk_direct_flush(buf, (size_t) written, PRINTK_PANIC_COLOR);
      restore_first_printk_value();
      return;
   }

   disable_preemption();
   {
      if (!old.first_printk) {

         /*
          * OK, we were the first. Now, flush our buffer directly and loop
          * flushing anything that, in the meanwhile, printk() calls from IRQs
          * generated.
          */

         printk_direct_flush(prefixbuf, (size_t) prefix_sz, PRINTK_COLOR);
         printk_direct_flush(buf, (size_t) written, PRINTK_COLOR);
         __printk_flush_ringbuf(buf, bufsz);

         /*
          * No need to call restore_first_printk_value(): printk_flush_ringbuf
          * clears the `first_printk` bit for us.
          */

      } else {

         /*
          * We were NOT the first printk on the stack: we're in an IRQ handler
          * and can only append our data to the ringbuf. On return, at some
          * point, the first printk(), in printk_flush_ringbuf() [case above]
          * will flush our data.
          */

         printk_append_to_ringbuf(prefixbuf, (size_t) prefix_sz);
         printk_append_to_ringbuf(buf, (size_t) written);
      }
   }
   enable_preemption();
}

static void
__regular_tilck_vprintk(u32 flags, const char *fmt, va_list args)
{
   char prefixbuf[PRINTK_PREFIXBUF_SZ];
   char buf[PRINTK_BUF_SZ];

   __tilck_vprintk(prefixbuf, buf, sizeof(buf), flags, fmt, args);
}

static void
__low_ssp_tilck_vprintk(u32 flags, const char *fmt, va_list args)
{
   char prefixbuf[PRINTK_PREFIXBUF_SZ];
   char buf[64];

   __tilck_vprintk(prefixbuf, buf, sizeof(buf), flags, fmt, args);
}

void
tilck_vprintk(u32 flags, const char *fmt, va_list args)
{
   static char p_prefixbuf[PRINTK_PREFIXBUF_SZ];
   static char p_buf[PRINTK_BUF_SZ];

   if (in_panic())
      __tilck_vprintk(p_prefixbuf, p_buf, sizeof(p_buf), flags, fmt, args);
   else if (get_rem_stack() < PRINTK_SAFE_STACK_SPACE)
      panic("No stack space for vprintk(\"%s\")", fmt);
   else if (get_rem_stack() < PRINTK_SAFE_STACK_SPACE + 512)
      __low_ssp_tilck_vprintk(flags, fmt, args);
   else
      __regular_tilck_vprintk(flags, fmt, args);
}

void printk(const char *fmt, ...)
{
   va_list args;
   va_start(args, fmt);
   vprintk(fmt, args);
   va_end(args);
}
