
#include <stringUtil.h>
#include <term.h>

void *memset(void *ptr, int value, size_t num)
{
   for (size_t i = 0; i < num; ++i)
      ((char*)ptr)[i] = value;
}

size_t strlen(const char *str)
{
   size_t c = 0;
   while (*str++) { ++c; }
   return c;
}

void itoa(int value, char *destBuf)
{
   const int base = 10;

   char * ptr;
   char * low;

   ptr = destBuf;
   // Set '-' for negative decimals.
   if (value < 0) {
     *ptr++ = '-';
   }

   // Remember where the numbers start.
   low = ptr;
   // The actual conversion.
   do
   {
     // Modulo is negative for negative value. This trick makes abs() unnecessary.
     *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz"[35 + value % base];
     value /= base;
   } while ( value );
   // Terminating the string.
   *ptr-- = '\0';
   // Invert the numbers.
   while ( low < ptr )
   {
     char tmp = *low;
     *low++ = *ptr;
     *ptr-- = tmp;
   }
}

void uitoa(unsigned int value, char *destBuf, unsigned int base)
{
   char * ptr;
   char * low;

   ptr = destBuf;

   // Remember where the numbers start.
   low = ptr;
   // The actual conversion.
   do
   {
      // Modulo is negative for negative value. This trick makes abs() unnecessary.
      *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz"[35 + value % base];
      value /= base;
   } while (value);
   // Terminating the string.
   *ptr-- = '\0';
   // Invert the numbers.
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
   char buf[32];

   while (*ptr) {

      if (*ptr == '%') {
         ++ptr;

         if (*ptr == '%')
            continue;

         switch (*ptr) {

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
            uitoa(va_arg(args, unsigned), buf, 16);
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
         continue;
      }

      term_write_char(*ptr);
      ++ptr;
   }
}

void printk(const char *fmt, ...)
{
   va_list args;
   va_start(args, fmt);
   vprintk(fmt, args);
}
