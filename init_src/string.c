
#include "string.h"
#include "usermode_syscall_wrappers.h"

void *memset(void *ptr, int value, size_t num)
{
   for (size_t i = 0; i < num; ++i)
      ((char*)ptr)[i] = value;

   return ptr;
}

void *memcpy(void *dest, void *src, size_t num)
{
   for (size_t i = 0; i < num; ++i)
      ((char*)dest)[i] = ((char*)src)[i];

   return dest;
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

#define BUF_SIZE 1024

void serialize(char *destBuf, size_t *ib, const char *str)
{
   while (*str) {

      if (*ib == BUF_SIZE) {
         // the buffer is full, write..
         write(1, destBuf, *ib);
         ib = 0;
      }

      destBuf[(*ib)++] = *str++;
   }
}

void vprintf(const char *fmt, va_list args)
{
   const char *ptr = fmt;
   char buf[32];
   char bigBuf[BUF_SIZE];
   size_t ib = 0;

   while (*ptr) {

      if (*ptr == '%') {
         ++ptr;

         if (*ptr == '%')
            continue;

         switch (*ptr) {

         case 'i':
            itoa(va_arg(args, int), buf);
            serialize(bigBuf, &ib, buf);
            break;

         case 'u':
            uitoa(va_arg(args, unsigned), buf, 10);
            serialize(bigBuf, &ib, buf);
            break;

         case 'x':
            uitoa(va_arg(args, unsigned), buf, 16);
            serialize(bigBuf, &ib, buf);
            break;

         case 's':
            serialize(bigBuf, &ib, va_arg(args, const char *));
            break;

         case 'p':
            uitoa(va_arg(args, unsigned), buf, 16);
            size_t len = strlen(buf);
            size_t fixedLen = 2 * sizeof(void*);

            serialize(bigBuf, &ib, "0x");

            while (fixedLen-- > len) {
               serialize(bigBuf, &ib, "0");
            }

            serialize(bigBuf, &ib, buf);
            break;

         default:
            serialize(bigBuf, &ib, "%");

            buf[0] = *ptr;
            buf[1] = 0;

            serialize(bigBuf, &ib, buf);
         }


         ++ptr;
         continue;
      }

      buf[0] = *ptr;
      buf[1] = 0;

      serialize(bigBuf, &ib, buf);

      ++ptr;
   }

   write(1, bigBuf, ib);
}

void printf(const char *fmt, ...)
{
   va_list args;
   va_start(args, fmt);
   vprintf(fmt, args);
}
