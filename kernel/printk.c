
#include <common/basic_defs.h>
#include <common/string_util.h>
#include <common/vga_textmode_defs.h>

#include <exos/term.h>
#include <exos/process.h>
#include <exos/interrupts.h>

#define PRINTK_COLOR COLOR_GREEN
#define PRINTK_RINGBUF_FLUSH_COLOR COLOR_CYAN
#define PRINTK_NOSPACE_IN_RBUF_FLUSH_COLOR COLOR_MAGENTA
#define PRINTK_PANIC_COLOR COLOR_GREEN

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


#define WRITE_CHAR(c)                                \
   do {                                              \
      if (!write_in_buf_char(&buf, buf_end, (c)))    \
         goto out;                                   \
   } while (0)

#define WRITE_STR(s)                                 \
   do {                                              \
      const char *__str = (s);                       \
      int sl = strlen(__str);                        \
      int rpad = MAX(0, right_padding - sl);         \
                                                     \
      if (!write_in_buf_str(&buf, buf_end, (__str))) \
         goto out;                                   \
                                                     \
      for (int i = 0; i < rpad; i++)                 \
         WRITE_CHAR(' ');                            \
   } while (0)


int vsnprintk(char *buf, size_t size, const char *fmt, va_list args)
{
   char *const initial_buf = buf;
   char *buf_end = buf + size;
   char intbuf[32];
   int right_padding = 0;

   while (*fmt) {

      if (*fmt != '%') {
         WRITE_CHAR(*fmt++);
         continue;
      }

      // *fmt is '%'
      ++fmt;

      if (*fmt == '%')
         continue;

switch_case:

      switch (*fmt) {

      case '-':
         fmt++;
         char pad_str_buf[16];
         char *p = pad_str_buf;

         while (*fmt && isdigit(*fmt)) {
            *p++ = *fmt++;
         }

         if (!*fmt)
            goto out; /* nothing after the %-<number> sequence */

         *p = 0;
         right_padding = atoi(pad_str_buf);

         /* parse now the command letter by re-entering in the switch case */
         goto switch_case;

      case 'l':

         if (!*++fmt)
            goto out;

         if (*fmt == 'l') {

            if (!*++fmt)
               goto out;

            if (*fmt == 'u') {
               uitoa64_dec(va_arg(args, u64), intbuf);
               WRITE_STR(intbuf);
            } else if (*fmt == 'i' || *fmt == 'd') {
               itoa64(va_arg(args, s64), intbuf);
               WRITE_STR(intbuf);
            }

         }
         break;

      case 'd':
      case 'i':
         itoa32(va_arg(args, s32), intbuf);
         WRITE_STR(intbuf);
         break;

      case 'u':
         uitoa32_dec(va_arg(args, u32), intbuf);
         WRITE_STR(intbuf);
         break;

      case 'x':
         uitoa32_hex(va_arg(args, u32), intbuf);
         WRITE_STR(intbuf);
         break;

      case 'c':
         WRITE_CHAR(va_arg(args, s32));

         if (right_padding > 0) {
            right_padding--;
            WRITE_STR(""); // write the padding
         }

         break;

      case 's':
         WRITE_STR(va_arg(args, const char *));
         break;

      case 'p':
         uitoa32_hex_fixed(va_arg(args, uptr), intbuf);
         WRITE_STR("0x");
         WRITE_STR(intbuf);
         break;

      default:
         WRITE_CHAR('%');
         WRITE_CHAR(*fmt);
      }

      right_padding = 0;
      ++fmt;
   }

out:
   buf[ buf < buf_end ? 0 : -1 ] = 0;
   return (buf - initial_buf);
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

typedef struct {

   union {

      struct {
         u32 used : 10;
         u32 read_pos : 10;
         u32 write_pos : 10;
         u32 in_printk : 1;
         u32 unused : 1;
      };

      u32 raw;
   };

} ringbuf_stat;

static char printk_rbuf[1024];
static volatile ringbuf_stat printk_rbuf_stat;

/*
 * NOTE: the ring buf cannot be larger than 1024 elems because the fields
 * 'used', 'read_pos' and 'write_pos' and 10 bits long and we CANNOT extend
 * them in 32 bits. Such approach is convenient because with everything packed
 * in 32 bits, we can do atomic operations.
 */
STATIC_ASSERT(sizeof(printk_rbuf) <= 1024);

static void printk_direct_flush(const char *buf, size_t size, u8 color)
{
   term_write2(buf, size, color);
}

void printk_flush_ringbuf(void)
{
   ringbuf_stat cs, ns;

   char minibuf[80];
   u32 to_read = 0;

   while (true) {

      do {
         cs = printk_rbuf_stat;
         ns = printk_rbuf_stat;

         /* We at most 'sizeof(minibuf)' bytes at a time */
         to_read = MIN(sizeof(minibuf), ns.used);

         /* And copy them to our minibuf */
         for (u32 i = 0; i < to_read; i++)
            minibuf[i] = printk_rbuf[(cs.read_pos + i) % sizeof(printk_rbuf)];

         /* Increase read_pos and decrease used */
         ns.read_pos = (ns.read_pos + to_read) % sizeof(printk_rbuf);
         ns.used -= to_read;

         if (!to_read)
            ns.in_printk = 0;

         /* Repeat that until we were able to do that atomically */

      } while (!BOOL_COMPARE_AND_SWAP(&printk_rbuf_stat.raw, cs.raw, ns.raw));

      /* Note: we check that in_printk in cs (current state) is unset! */
      if (!to_read)
         break;

      printk_direct_flush(minibuf, to_read, PRINTK_RINGBUF_FLUSH_COLOR);
   }
}

static void printk_append_to_ringbuf(const char *buf, size_t size)
{
   static const char err_msg[] = "{_DROPPED_}\n";

   ringbuf_stat cs, ns;

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

   } while (!BOOL_COMPARE_AND_SWAP(&printk_rbuf_stat.raw, cs.raw, ns.raw));

   // Now we have some allocated space in the ringbuf

   for (u32 i = 0; i < size; i++)
      printk_rbuf[(cs.write_pos + i) % sizeof(printk_rbuf)] = buf[i];
}

void vprintk(const char *fmt, va_list args)
{
   char buf[256];
   int written = 0;
   bool prefix = in_panic() ? false : true;

   if (*fmt == PRINTK_CTRL_CHAR) {
      u32 cmd = *(u32 *)fmt;
      fmt += 4;

      if (cmd == *(u32 *)NO_PREFIX)
         prefix = false;
   }

   if (prefix)
      written = snprintk(buf, sizeof(buf), "[kernel] ");

   written += vsnprintk(buf + written, sizeof(buf) - written, fmt, args);

   if (!term_is_initialized()) {
      printk_append_to_ringbuf(buf, written);
      return;
   }

   if (in_panic()) {
      printk_direct_flush(buf, written, PRINTK_PANIC_COLOR);
      return;
   }

   disable_preemption();
   {
      ringbuf_stat cs, ns;

      do {
         cs = printk_rbuf_stat;
         ns = printk_rbuf_stat;
         ns.in_printk = 1;
      } while (!BOOL_COMPARE_AND_SWAP(&printk_rbuf_stat.raw, cs.raw, ns.raw));

      if (!cs.in_printk) {

         printk_direct_flush(buf, written, PRINTK_COLOR);
         printk_flush_ringbuf();

      } else {

         /* in_printk was already 1 */
         printk_append_to_ringbuf(buf, written);
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
