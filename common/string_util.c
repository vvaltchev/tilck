
#define __STRING_UTIL_C__

#include <common/basic_defs.h>
#include <common/failsafe_assert.h>
#include <common/string_util.h>

#define DIGITS "0123456789abcdef"

#define instantiate_uitoa_hex_fixed(func_name, bits)       \
   void func_name(u##bits value, char *buf)                \
   {                                                       \
      u32 j = sizeof(value) * 8 - 4;                       \
      char *ptr = buf;                                     \
                                                           \
      for (u32 i = 0; i < sizeof(value) * 2; i++, j-=4) {  \
         *ptr++ = DIGITS[(value >> j) & 0xf];              \
      }                                                    \
                                                           \
      *ptr = 0;                                            \
   }

#define instantiate_uitoa(func_name, bits, base)           \
   void func_name(u##bits value, char *buf)                \
   {                                                       \
      char *ptr = buf;                                     \
                                                           \
      while (value) {                                      \
         *ptr++ = DIGITS[value % base];                    \
         value /= base;                                    \
      }                                                    \
                                                           \
      if (ptr == buf)                                      \
         *ptr++ = DIGITS[0];                               \
                                                           \
      *ptr = 0;                                            \
      str_reverse(buf, ptr - buf);                         \
   }

#define instantiate_itoa(func_name, bits)                  \
   void func_name(s##bits svalue, char *buf)               \
   {                                                       \
      char *ptr = buf;                                     \
                                                           \
      if (svalue == 0) {                                   \
         *ptr++ = DIGITS[0];                               \
         *ptr = 0;                                         \
         return;                                           \
      }                                                    \
                                                           \
      u##bits value = svalue > 0 ? svalue : -svalue;       \
                                                           \
      while (value) {                                      \
         *ptr++ = DIGITS[value % 10];                      \
         value /= 10;                                      \
      }                                                    \
                                                           \
      if (svalue < 0)                                      \
         *ptr++ = '-';                                     \
                                                           \
      *ptr = 0;                                            \
      str_reverse(buf, ptr - buf);                         \
   }


instantiate_uitoa_hex_fixed(uitoa32_hex_fixed, 32)
instantiate_uitoa_hex_fixed(uitoa64_hex_fixed, 64)

instantiate_uitoa(uitoa32_dec, 32, 10)
instantiate_uitoa(uitoa64_dec, 64, 10)
instantiate_uitoa(uitoa32_hex, 32, 16)
instantiate_uitoa(uitoa64_hex, 64, 16)

instantiate_itoa(itoa32, 32)
instantiate_itoa(itoa64, 64)


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
   while(*s1 && tolower(*s1) == tolower(*s2)) {
      s1++; s2++;
   }

   return (int)tolower(*s1) - (int)tolower(*s2);
}

/*
 * Reverse a string in-place.
 * NOTE: len == strlen(str): it does NOT include the final \0.
 */
inline void str_reverse(char *str, size_t len)
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

int exos_atoi(const char *str)
{
   int res = 0;
   int sign = 1;
   const char *p;

   if (*str == '-') {
      sign = -1;
      str++;
   }

   for (p = str; *p; p++) {

      if (!isdigit(*p))
         return 0; // invalid number

      res = res * 10 + sign * (*p - '0');

      if ((sign > 0) != (res > 0))
         return 0; // signed int overflow
   }

   return res;
}
