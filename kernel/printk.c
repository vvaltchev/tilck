
#include <common/basic_defs.h>
#include <common/string_util.h>
#include <common/arch/generic_x86/vga_textmode_defs.h>
#include <exos/term.h>
#include <exos/process.h>


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
   return (buf - initial_buf + 1);
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

void vprintk(const char *fmt, va_list args)
{
   char buf[256];
   char *p = buf;
   int written;

   written = snprintk(buf, sizeof(buf), "[kernel] ");
   vsnprintk(buf + written - 1, sizeof(buf) - written + 1, fmt, args);

   disable_preemption();
   {
      u8 curr_color = term_get_color();
      term_set_color(make_color(COLOR_LIGHT_RED, COLOR_BLACK));

      while (*p)
         term_write_char(*p++);

      term_set_color(curr_color);
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
