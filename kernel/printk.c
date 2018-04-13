
#include <common/basic_defs.h>
#include <common/string_util.h>
#include <common/arch/generic_x86/vga_textmode_defs.h>

#include <exos/term.h>
#include <exos/process.h>
#include <exos/interrupts.h>

#define PRINTK_COLOR COLOR_GREEN
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

#define WRITE_STR(s)                                 \
   do {                                              \
      if (!write_in_buf_str(&buf, buf_end, (s)))     \
         goto out;                                   \
   } while (0)

#define WRITE_CHAR(c)                                \
   do {                                              \
      if (!write_in_buf_char(&buf, buf_end, (c)))    \
         goto out;                                   \
   } while (0)


int vsnprintk(char *buf, size_t size, const char *fmt, va_list args)
{
   char *const initial_buf = buf;
   char *buf_end = buf + size;
   char intbuf[32];

   while (*fmt) {

      if (*fmt != '%') {
         WRITE_CHAR(*fmt++);
         continue;
      }

      // *fmt is '%'
      ++fmt;

      if (*fmt == '%')
         continue;

      switch (*fmt) {

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
         term_write_char(va_arg(args, s32));
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

static volatile bool in_printk;
static char printk_ringbuf[1024];
static volatile ringbuf_stat printk_ringbuf_stat;

STATIC_ASSERT(sizeof(printk_ringbuf) <= 1024);

static void printk_raw_flush(char *buf, size_t size)
{
   for (u32 i = 0; i < size; i++)
      term_write_char(buf[i]);
}

static void printk_flush(char *buf, size_t size)
{
   u8 curr_color = term_get_color();
   term_set_color(make_color(PRINTK_COLOR, COLOR_BLACK));

   printk_raw_flush(buf, size);

   // flush the ring buffer ...
   ringbuf_stat cs, ns;

   while (true) {

      do {
         cs = printk_ringbuf_stat;
         ns = printk_ringbuf_stat;

         ns.read_pos = (ns.read_pos + ns.used) % sizeof(printk_ringbuf);
         ns.used = 0;
         ns.in_printk = 0;

      } while (!BOOL_COMPARE_AND_SWAP(&printk_ringbuf_stat.raw, cs.raw, ns.raw));

      if (!cs.in_printk)
         goto out;

      const u32 rp = cs.read_pos;
      for (u32 i = 0; i < cs.used; i++) {
         term_write_char(printk_ringbuf[(rp + i) % sizeof(printk_ringbuf)]);
      }
   }

out:
   term_set_color(curr_color);
}

static void printk_append_to_ringbuf(char *buf, size_t size)
{
   ringbuf_stat cs, ns;

   do {
      cs = printk_ringbuf_stat;
      ns = printk_ringbuf_stat;

      if (cs.used + size > sizeof(printk_ringbuf)) {
         printk_raw_flush(buf, size);
         return;
      }

      ns.used += size;
      ns.write_pos = (ns.write_pos + size) % sizeof(printk_ringbuf);

   } while (!BOOL_COMPARE_AND_SWAP(&printk_ringbuf_stat.raw, cs.raw, ns.raw));

   // Now we have some allocated space in the ringbuf

   for (u32 i = 0; i < size; i++)
      printk_ringbuf[(cs.write_pos + i) % sizeof(printk_ringbuf)] = buf[i];
}

void vprintk(const char *fmt, va_list args)
{
   char buf[256];
   int written;

   written = snprintk(buf, sizeof(buf), "[kernel] ");
   written += vsnprintk(buf + written, sizeof(buf) - written, fmt, args);

   if (in_panic) {
      term_set_color(make_color(PRINTK_PANIC_COLOR, COLOR_BLACK));
      printk_raw_flush(buf, written);
      return;
   }

   disable_preemption();
   {
      ringbuf_stat cs, ns;

      do {
         cs = printk_ringbuf_stat;
         ns = printk_ringbuf_stat;
         ns.in_printk = 1;
      } while (!BOOL_COMPARE_AND_SWAP(&printk_ringbuf_stat.raw, cs.raw, ns.raw));

      if (!cs.in_printk) {

         /*
          * in_printk was 0 and we set it to 1.
          * printk_flush() will restore it to 0.
          */
         printk_flush(buf, written);

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
