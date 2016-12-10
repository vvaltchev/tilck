
#include <stringUtil.h>
#include <term.h>

void itoa(intptr_t value, char *destBuf)
{
   const intptr_t base = 10;

   char *ptr;
   char *low;

   ptr = destBuf;

   if (value < 0) {
     *ptr++ = '-';
   }

   low = ptr;

   do
   {
     // Modulo is negative for negative value. This trick makes abs() unnecessary.
     *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz"[35 + value % base];
     value /= base;
   } while ( value );

   *ptr-- = '\0';

   while ( low < ptr )
   {
     char tmp = *low;
     *low++ = *ptr;
     *ptr-- = tmp;
   }
}

void llitoa(s64 value, char *destBuf)
{
   const intptr_t base = 10;

   char *ptr;
   char *low;

   ptr = destBuf;

   if (value < 0) {
      *ptr++ = '-';
   }

   low = ptr;

   do
   {
      // Modulo is negative for negative value. This trick makes abs() unnecessary.
      *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz"[35 + value % base];
      value /= base;
   } while (value);

   *ptr-- = '\0';

   while (low < ptr)
   {
      char tmp = *low;
      *low++ = *ptr;
      *ptr-- = tmp;
   }
}

void uitoa(uintptr_t value, char *destBuf, u32 base)
{
   char *ptr;
   char *low;

   ptr = destBuf;
   low = ptr;

   do
   {
      // Modulo is negative for negative value. This trick makes abs() unnecessary.
      *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz"[35 + value % base];
      value /= base;
   } while (value);

   *ptr-- = '\0';
   while (low < ptr)
   {
      char tmp = *low;
      *low++ = *ptr;
      *ptr-- = tmp;
   }
}

void ullitoa(u64 value, char *destBuf, u32 base)
{
   char *ptr;
   char *low;

   ptr = destBuf;
   low = ptr;

   do
   {
      // Modulo is negative for negative value. This trick makes abs() unnecessary.
      *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz"[35 + value % base];
      value /= base;
   } while (value);

   *ptr-- = '\0';
   while (low < ptr)
   {
      char tmp = *low;
      *low++ = *ptr;
      *ptr-- = tmp;
   }
}


void vprintk(const char *fmt, va_list args)
{
   const char *ptr = fmt;
   char buf[64];

   while (*ptr) {

      if (*ptr != '%') {
         term_write_char(*ptr++);
         continue;
      }

      // *ptr is '%'

      ++ptr;

      if (*ptr == '%')
         continue;

      switch (*ptr) {

      case 'l':
         ++ptr;

         if (*ptr && *ptr == 'l') {
            ++ptr;
            if (*ptr) {
               if (*ptr == 'u') {
                  ullitoa(va_arg(args, u64), buf, 10);
                  term_write_string(buf);
               } else if (*ptr == 'i' || *ptr == 'd') {
                  llitoa(va_arg(args, s64), buf);
                  term_write_string(buf);
               }
            }
         }
         break;

      case 'd':
      case 'i':
         itoa(va_arg(args, int), buf);
         term_write_string(buf);
         break;

      case 'u':
         uitoa(va_arg(args, unsigned), buf, 10);
         term_write_string(buf);
         break;

      case 'x':
         uitoa(va_arg(args, unsigned), buf, 16);
         term_write_string(buf);
         break;

      case 's':
         term_write_string(va_arg(args, const char *));
         break;

      case 'p':
         uitoa(va_arg(args, uintptr_t), buf, 16);
         size_t len = strlen(buf);
         size_t fixedLen = 2 * sizeof(void*);

         term_write_string("0x");

         while (fixedLen-- > len) {
            term_write_char('0');
         }

         term_write_string(buf);
         break;

      default:
         term_write_char('%');
         term_write_char(*ptr);
      }


      ++ptr;
   }
}

void printk(const char *fmt, ...)
{
   va_list args;
   va_start(args, fmt);
   vprintk(fmt, args);
}
