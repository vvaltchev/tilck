/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_console.h>

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

#define PRINTK_COLOR                          COLOR_GREEN
#define PRINTK_RINGBUF_FLUSH_COLOR            COLOR_CYAN
#define PRINTK_NOSPACE_IN_RBUF_FLUSH_COLOR    COLOR_MAGENTA
#define PRINTK_PANIC_COLOR                    COLOR_GREEN

static bool
write_in_buf_str(char **buf_ref, char *buf_end, const char *s)
{
   char *ptr = *buf_ref;

   while (*s && ptr < buf_end) {
      *ptr++ = *s++;
   }

   *buf_ref = ptr;
   return ptr < buf_end;
}

static inline bool
write_in_buf_char(char **buf_ref, char *buf_end, char c)
{
   char *ptr = *buf_ref;
   *ptr++ = c;
   *buf_ref = ptr;
   return ptr < buf_end;
}

struct snprintk_ctx {

   int left_padding;
   int right_padding;
   char *buf;
   char *buf_end;
   bool zero_lpad;
   bool hash_sign;
};

static void
snprintk_ctx_reset_per_argument_state(struct snprintk_ctx *ctx)
{
   ctx->left_padding = 0;
   ctx->right_padding = 0;
   ctx->zero_lpad = false;
   ctx->hash_sign = false;
}

#define WRITE_CHAR(c)                                         \
   do {                                                       \
      if (!write_in_buf_char(&ctx->buf, ctx->buf_end, (c)))   \
         goto out;                                            \
   } while (0)

#define WRITE_STR(s) if (!write_str(ctx, *fmt, s)) goto out;

static bool
write_str(struct snprintk_ctx *ctx, char fmtX, const char *str)
{
   int sl = (int) strlen(str);
   int lpad = MAX(0, ctx->left_padding - sl);
   int rpad = MAX(0, ctx->right_padding - sl);
   char pad_char = ' ';

   /* Cannot have both left padding _and_ right padding */
   ASSERT(!lpad || !rpad);

   if (ctx->hash_sign) {

      int off = 0;

      if (fmtX == 'x')
         off = 2;
      else if (fmtX == 'o')
         off = 1;

      lpad = MAX(0, lpad - off);
      rpad = MAX(0, rpad - off);
   }

   if (ctx->zero_lpad) {

      pad_char = '0';

      if (ctx->hash_sign && (fmtX == 'x' || fmtX == 'o')) {

         WRITE_CHAR('0');

         if (fmtX == 'x')
            WRITE_CHAR('x');
      }
   }

   for (int i = 0; i < lpad; i++)
      WRITE_CHAR(pad_char);

   if (ctx->hash_sign && pad_char != '0') {

      if (fmtX == 'x' || fmtX == 'o') {

         WRITE_CHAR('0');

         if (fmtX == 'x')
            WRITE_CHAR('x');
      }
   }

   if (!write_in_buf_str(&ctx->buf, ctx->buf_end, (str)))
      goto out;

   for (int i = 0; i < rpad; i++)
      WRITE_CHAR(pad_char);

   return true;

out:
   return false;
}

#define UOX_ITOA(base)                                   \
   if (fmt[-1] == '%')                                   \
      uitoa32(va_arg(args, u32), intbuf, base);          \
   else /* fmt[-1] is 'l' or 'z' */                      \
      uitoaN(va_arg(args, ulong), intbuf, base);

static u8 diuox_base[128] =
{
   ['d'] = 10,
   ['i'] = 10,
   ['u'] = 10,
   ['o'] = 8,
   ['x'] = 16,
};

int vsnprintk(char *initial_buf, size_t size, const char *fmt, va_list args)
{
   char intbuf[64];

   struct snprintk_ctx __ctx = {
      .left_padding = 0,
      .right_padding = 0,
      .zero_lpad = false,
      .buf = initial_buf,
      .buf_end = initial_buf + size,
   };

   /* ctx has to be a pointer because of macros shared with WRITE_STR */
   struct snprintk_ctx *ctx = &__ctx;

   while (*fmt) {

      // *fmt != '%', just write it and continue.
      if (*fmt != '%') {
         WRITE_CHAR(*fmt++);
         continue;
      }

      // *fmt is '%' ...
      ++fmt;

      // after the '%' ...

      if (*fmt == '%' || (u8)*fmt >= 128) {
         /* %% or % followed by non-ascii char */
         WRITE_CHAR(*fmt++);
         continue;
      }

      // after the '%', follows an ASCII char != '%' ...

switch_case:

      switch (*fmt) {

      case '0':
         ctx->zero_lpad = true;

         if (!*++fmt)
            goto truncated_seq;

         /* parse now the command letter by re-entering in the switch case */
         goto switch_case;

      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':

         ctx->left_padding = (int)tilck_strtol(fmt, &fmt, 10, NULL);

         if (!*fmt)
            goto truncated_seq;

         /* parse now the command letter by re-entering in the switch case */
         goto switch_case;

      case '-':
         ctx->right_padding = (int)tilck_strtol(fmt + 1, &fmt, 10, NULL);

         if (!*fmt)
            goto truncated_seq;

         /* parse now the command letter by re-entering in the switch case */
         goto switch_case;

      case '#':

         if (fmt[-1] != '%')
            break; /* ignore misplaced '#' */

         if (!*++fmt)
            goto truncated_seq;

         ctx->hash_sign = true;
         goto switch_case;

      // %z (followed by d, i, o, u, x) is C99 prefix for size_t
      case 'z':

         if (!*++fmt)
            goto truncated_seq;

         goto switch_case;

      // %l makes the following type (d, i, o, u, x) a long.
      case 'l':

         if (!*++fmt)
            goto truncated_seq;

         // %ll make the following type a long long.
         if (*fmt == 'l') {

            if (!*++fmt)
               goto truncated_seq;

            if (!diuox_base[(u8)*fmt])
               goto incomplete_seq;

            if (*fmt == 'i' || *fmt == 'd')
               itoa64(va_arg(args, s64), intbuf);
            else
               uitoa64(va_arg(args, u64), intbuf, diuox_base[(u8)*fmt]);

            WRITE_STR(intbuf);
            break;
         }

         goto switch_case;

      case 'c':
         WRITE_CHAR((char) va_arg(args, s32));

         if (ctx->right_padding > 0) {
            ctx->right_padding--;
            WRITE_STR(""); // write the padding
         }

         break;

      case 's':
         WRITE_STR(va_arg(args, const char *));
         break;

      case 'p':
         uitoaN_hex_fixed(va_arg(args, ulong), intbuf);
         WRITE_STR("0x");
         WRITE_STR(intbuf);
         break;

      default:

         if (diuox_base[(u8)*fmt]) {

            if (*fmt == 'd' || *fmt == 'i') {

               if (fmt[-1] == '%')
                  itoa32(va_arg(args, s32), intbuf);
               else /* 'l' or 'z' */
                  itoaN(va_arg(args, long), intbuf);

            } else {

               UOX_ITOA(diuox_base[(u8)*fmt]);
            }

            WRITE_STR(intbuf);

         } else {

incomplete_seq:

            WRITE_CHAR('%');

            if (ctx->hash_sign)
               WRITE_CHAR('#');

            WRITE_CHAR(*fmt);
         }
      }

      snprintk_ctx_reset_per_argument_state(ctx);
      ++fmt;
   }

out:
truncated_seq:
   ctx->buf[ ctx->buf < ctx->buf_end ? 0 : -1 ] = 0;
   return (int)(ctx->buf - initial_buf);
}

int snprintk(char *buf, size_t size, const char *fmt, ...)
{
   int written;

   va_list args;
   va_start(args, fmt);
   written = vsnprintk(buf, size, fmt, args);
   va_end(args);

   return written;
}

struct ringbuf_stat {

   union {

      struct {
         u32 used : 10;
         u32 read_pos : 10;
         u32 write_pos : 10;
         u32 in_printk : 1;
         u32 unused : 1;
      };

      ATOMIC(u32) raw;
      u32 __raw;
   };
};

static char printk_rbuf[1024];
static volatile struct ringbuf_stat printk_rbuf_stat;
bool __in_printk;

/*
 * NOTE: the ring buf cannot be larger than 1024 elems because the fields
 * 'used', 'read_pos' and 'write_pos' are 10 bits long and we CANNOT extend
 * them in 32 bits. Such approach is convenient because with everything packed
 * in 32 bits, we can do atomic operations.
 */
STATIC_ASSERT(sizeof(printk_rbuf) <= 1024);

static void printk_direct_flush_no_tty(const char *buf, size_t size, u8 color)
{
   /*
    * tty has not been initialized yet, therefore we have to translate here
    * \n to \r\n, by writing character by character to term.
    */

   for (u32 i = 0; i < size; i++) {

      __in_printk = true;
      {
         if (buf[i] == '\n')
            term_write("\r", 1, color);

         term_write(&buf[i], 1, color);
      }
      __in_printk = false;
   }
}

static void printk_direct_flush(const char *buf, size_t size, u8 color)
{
   if (UNLIKELY(get_curr_tty() == NULL))
      return printk_direct_flush_no_tty(buf, size, color);

   /* tty has been initialized and set a term write filter func */
   __in_printk = true;
   {
      if (KRN_PRINTK_ON_CURR_TTY || !get_curr_process_tty())
         term_write(buf, size, color);
      else
         tty_curr_proc_write(buf, size);
   }
   __in_printk = false;
   return;
}

void printk_flush_ringbuf(void)
{
   struct ringbuf_stat cs, ns;

   char minibuf[80];
   u32 to_read = 0;

   while (true) {

      do {
         cs = printk_rbuf_stat;
         ns = printk_rbuf_stat;

         /* We at most 'sizeof(minibuf)' bytes at a time */
         to_read = UNSAFE_MIN(sizeof(minibuf), ns.used);

         /* And copy them to our minibuf */
         for (u32 i = 0; i < to_read; i++)
            minibuf[i] = printk_rbuf[(cs.read_pos + i) % sizeof(printk_rbuf)];

         /* Increase read_pos and decrease used */
         ns.read_pos = (ns.read_pos + to_read) % sizeof(printk_rbuf);
         ns.used -= to_read;

         if (!to_read)
            ns.in_printk = 0;

         /* Repeat that until we were able to do that atomically */

      } while (!atomic_cas_weak(&printk_rbuf_stat.raw,
                                &cs.__raw,
                                ns.__raw,
                                mo_relaxed,
                                mo_relaxed));

      /* Note: we check that in_printk in cs (current state) is unset! */
      if (!to_read)
         break;

      printk_direct_flush(minibuf, to_read, PRINTK_RINGBUF_FLUSH_COLOR);
   }
}

static void printk_append_to_ringbuf(const char *buf, size_t size)
{
   static const char err_msg[] = "{_DROPPED_}\n";

   struct ringbuf_stat cs, ns;

   do {
      cs = printk_rbuf_stat;
      ns = printk_rbuf_stat;

      if (cs.used + size >= sizeof(printk_rbuf)) {

         if (term_is_initialized()) {
            printk_direct_flush(buf, size, PRINTK_NOSPACE_IN_RBUF_FLUSH_COLOR);
            return;
         }

         if (buf != err_msg && cs.used < sizeof(printk_rbuf) - 1) {
            size = MIN(sizeof(printk_rbuf) - cs.used - 1, sizeof(err_msg));
            printk_append_to_ringbuf(err_msg, size);
         }

         return;
      }

      ns.used += size;
      ns.write_pos = (ns.write_pos + size) % sizeof(printk_rbuf);

   } while (!atomic_cas_weak(&printk_rbuf_stat.raw,
                             &cs.__raw,
                             ns.__raw,
                             mo_relaxed,
                             mo_relaxed));

   // Now we have some allocated space in the ringbuf

   for (u32 i = 0; i < size; i++)
      printk_rbuf[(cs.write_pos + i) % sizeof(printk_rbuf)] = buf[i];
}

void vprintk(const char *fmt, va_list args)
{
   static const char truncated_str[] = "[...]";

   char buf[256];
   int written = 0;
   bool prefix = in_panic() ? false : true;

   if (*fmt == PRINTK_CTRL_CHAR) {
      u32 cmd = *(u32 *)fmt;
      fmt += 4;

      if (cmd == *(u32 *)NO_PREFIX)
         prefix = false;
   }

   if (prefix) {

      const u64 systime = get_sys_time();

      written = snprintk(
         buf, sizeof(buf), "[%5u.%03u] ",
         (u32)(systime / TS_SCALE),
         (u32)((systime % TS_SCALE) / (TS_SCALE / 1000))
      );
   }

   written += vsnprintk(buf + written, sizeof(buf) - (u32)written, fmt, args);

   if (written == sizeof(buf)) {

      /*
       * Corner case: the buffer is completely full and the final \0 has been
       * included in 'written'.
       */

      memcpy(buf + sizeof(buf) - sizeof(truncated_str),
             truncated_str,
             sizeof(truncated_str));

      written--;
   }

   if (!term_is_initialized()) {
      printk_append_to_ringbuf(buf, (size_t) written);
      return;
   }

   if (in_panic()) {
      printk_direct_flush(buf, (size_t) written, PRINTK_PANIC_COLOR);
      return;
   }

   disable_preemption();
   {
      struct ringbuf_stat cs, ns;

      do {
         cs = printk_rbuf_stat;
         ns = printk_rbuf_stat;
         ns.in_printk = 1;
      } while (!atomic_cas_weak(&printk_rbuf_stat.raw,
                                &cs.__raw,
                                ns.__raw,
                                mo_relaxed,
                                mo_relaxed));

      if (!cs.in_printk) {

         printk_direct_flush(buf, (size_t) written, PRINTK_COLOR);
         printk_flush_ringbuf();

      } else {

         /* in_printk was already 1 */
         printk_append_to_ringbuf(buf, (size_t) written);
      }

   }
   enable_preemption();
}

void printk(const char *fmt, ...)
{
   va_list args;
   va_start(args, fmt);
   vprintk(fmt, args);
   va_end(args);
}
