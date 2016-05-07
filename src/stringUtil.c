
#include <stringUtil.h>
#include <term.h>

void memcpy(void *dest, const void *src, size_t n)
{
   // Copy byte-by-byte until 'b' because divisible by 4
   asmVolatile ("cld\n\t"
                "rep movsb\n\t"
                : // no output
                :"c" (n % 4),"S" (src),"D" (dest)
                :"memory");

   // Copy a dword at time the rest
   asmVolatile ("rep movsd\n\t"
                : // no output
                : "c" (n / 4)
                : "memory");
}

// Dest and src can overlap
void memmove(void *dest, const void *src, size_t n)
{
   if (dest < src) {

      memcpy(dest, src, n);

   } else {
      asmVolatile ("std\n\t"
                   "rep movsb\n\t"
                   : // no output
                   :"c" (n), "S" (src+n-1), "D" (dest+n-1)
                   :"memory");
   }
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
