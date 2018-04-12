
#define __STRING_UTIL_C__

#include <common/string_util.h>
#include <common/basic_term.h>

#define MAGIC_ITOA_STRING \
   "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz"

#define instantiate_generic_itoa(function_name, integer_type)   \
   void function_name(integer_type value, char *destBuf)        \
   {                                                            \
      const integer_type base = 10;                             \
      char *ptr = destBuf;                                      \
                                                                \
      if (value < 0) {                                          \
         *ptr++ = '-';                                          \
      }                                                         \
                                                                \
      char *low = ptr;                                          \
                                                                \
      do {                                                      \
         /* Mod(x, b) < 0 if x < 0: no need for abs(). */       \
         *ptr++ = MAGIC_ITOA_STRING[35 + value % base];         \
         value /= base;                                         \
      } while ( value );                                        \
                                                                \
      *ptr-- = '\0';                                            \
                                                                \
      while (low < ptr) {                                       \
         char tmp = *low;                                       \
         *low++ = *ptr;                                         \
         *ptr-- = tmp;                                          \
      }                                                         \
   }

instantiate_generic_itoa(itoa32, s32)
instantiate_generic_itoa(itoa64, s64)

static const char digits[] = "0123456789abcdef";

#define instantiate_uitoa_hex(func_name, int_type)       \
   void func_name(int_type value, char *buf)             \
   {                                                     \
      char *ptr = buf;                                   \
                                                         \
      for (u32 i = 0; i < sizeof(value) * 2; i++) {      \
         *ptr++ = digits[value & 0xf];                   \
         value >>= 4;                                    \
      }                                                  \
                                                         \
      ptr--;                                             \
      while (*ptr == '0' && ptr > buf) { *ptr-- = 0; }   \
      *++ptr = 0;                                        \
                                                         \
      str_reverse(buf, ptr - buf);                       \
   }

#define instantiate_uitoa_hex_fixed(func_name, int_type)   \
   void func_name(int_type value, char *buf)               \
   {                                                       \
      u32 j = sizeof(value) * 8 - 4;                       \
      char *ptr = buf;                                     \
                                                           \
      for (u32 i = 0; i < sizeof(value) * 2; i++, j-=4) {  \
         *ptr++ = digits[(value >> j) & 0xf];              \
      }                                                    \
                                                           \
      *ptr = 0;                                            \
   }

#define instantiate_uitoa_dec(func_name, int_type)       \
   void func_name(int_type value, char *buf)             \
   {                                                     \
      char *ptr = buf;                                   \
                                                         \
      while (value) {                                    \
         *ptr++ = digits[value % 10];                    \
         value /= 10;                                    \
      }                                                  \
                                                         \
      if (ptr == buf)                                    \
         *ptr++ = digits[0];                             \
                                                         \
      *ptr = 0;                                          \
      str_reverse(buf, ptr - buf);                       \
   }

instantiate_uitoa_hex(uitoa32_hex, u32)
instantiate_uitoa_hex_fixed(uitoa32_hex_fixed, u32)
instantiate_uitoa_hex(uitoa64_hex, u64)
instantiate_uitoa_hex_fixed(uitoa64_hex_fixed, u64)

instantiate_uitoa_dec(uitoa32_dec, u32)
instantiate_uitoa_dec(uitoa64_dec, u64)


int strcmp(const char *s1, const char *s2)
{
   while(*s1 && *s1 == *s2) {
      s1++; s2++;
   }

   return (int)*s1 - (int)*s2;
}

int strncmp(const char *s1, const char *s2, size_t n)
{
   size_t i = 0;

   while(i < n && *s1 && *s1 == *s2) {
      s1++; s2++; i++;
   }

   return i == n ? 0 : (int)*s1 - (int)*s2;
}

int stricmp(const char *s1, const char *s2)
{
   while(*s1 && lower(*s1) == lower(*s2)) {
      s1++; s2++;
   }

   return (int)lower(*s1) - (int)lower(*s2);
}

/*
 * Reverse a string in-place.
 * NOTE: len == strlen(str): it does NOT include the final \0.
 */
void str_reverse(char *str, size_t len)
{
   ASSERT(len == strlen(str));

   if (!len)
      return;

   char *end = str + len - 1;

   while (str < end) {

      *str ^= *end;
      *end ^= *str;
      *str ^= *end;

      str++;
      end--;
   }
}

static void print_string(const char *s)
{
   while (*s)
      term_write_char(*s++);
}

void vprintk(const char *fmt, va_list args)
{
   const char *ptr = fmt;
   char buf[64];

   print_string("[kernel] ");

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
                  uitoa64_dec(va_arg(args, u64), buf);
                  print_string(buf);
               } else if (*ptr == 'i' || *ptr == 'd') {
                  itoa64(va_arg(args, s64), buf);
                  print_string(buf);
               }
            }
         }
         break;

      case 'd':
      case 'i':
         itoa32(va_arg(args, s32), buf);
         print_string(buf);
         break;

      case 'u':
         uitoa32_dec(va_arg(args, u32), buf);
         print_string(buf);
         break;

      case 'x':
         uitoa32_hex(va_arg(args, u32), buf);
         print_string(buf);
         break;

      case 'c':
         term_write_char(va_arg(args, s32));
         break;

      case 's':
         print_string(va_arg(args, const char *));
         break;

      case 'p':
         uitoa32_hex_fixed(va_arg(args, uptr), buf);
         print_string("0x");
         print_string(buf);
         break;

      default:
         term_write_char('%');
         term_write_char(*ptr);
      }

      ++ptr;
   }
}

void WEAK printk(const char *fmt, ...)
{
   va_list args;
   va_start(args, fmt);
   vprintk(fmt, args);
}

