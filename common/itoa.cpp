
/* SPDX-License-Identifier: BSD-2-Clause */

extern "C" {
   #include <tilck/common/basic_defs.h>
   #include <tilck/common/string_util.h>
   #include <tilck/kernel/errno.h>
}

#include <tilck/common/cpputils.h>

#define DIGITS "0123456789abcdef"

template <typename T>
void __uitoa_fixed(T value, char *buf)
{
   u32 j = sizeof(value) * 8 - 4;
   char *ptr = buf;

   for (u32 i = 0; i < sizeof(value) * 2; i++, j-=4) {
      *ptr++ = DIGITS[(value >> j) & 0xf];
   }

   *ptr = 0;
}

template <typename T, u32 base>
void __uitoa(T value, char *buf)
{
   char *ptr = buf;

   while (value) {
      *ptr++ = DIGITS[value % base];
      value /= base;
   }

   if (ptr == buf)
      *ptr++ = DIGITS[0];

   *ptr = 0;
   str_reverse(buf, (size_t)ptr - (size_t)buf);
}

template <typename T, u32 base>
void __itoa(T svalue, char *buf)
{
   typedef typename unsigned_type<T>::type U;
   char *ptr = buf;

   if (svalue == 0) {
      *ptr++ = DIGITS[0];
      *ptr = 0;
      return;
   }

   U value = svalue >= 0 ? (U) svalue : (U) -svalue;

   while (value) {
      *ptr++ = DIGITS[value % 10];
      value /= 10;
   }

   if (svalue < 0)
      *ptr++ = '-';

   *ptr = 0;
   str_reverse(buf, (size_t)ptr - (size_t)buf);
}

#define instantiate_uitoa_hex_fixed(func_name, bits)       \
   void func_name(u##bits value, char *buf) {              \
      __uitoa_fixed(value, buf);                           \
   }

#define instantiate_uitoa(func_name, bits, base)           \
   void func_name(u##bits value, char *buf) {              \
      __uitoa<u##bits, base>(value, buf);                  \
   }

#define instantiate_itoa(func_name, bits, base)            \
   void func_name(s##bits svalue, char *buf) {             \
      __itoa<s##bits, base>(svalue, buf);                  \
   }

extern "C" {
   instantiate_uitoa_hex_fixed(uitoa32_hex_fixed, 32)
   instantiate_uitoa_hex_fixed(uitoa64_hex_fixed, 64)

   instantiate_uitoa(uitoa32_dec, 32, 10)
   instantiate_uitoa(uitoa64_dec, 64, 10)
   instantiate_uitoa(uitoa32_oct, 32, 8)
   instantiate_uitoa(uitoa64_oct, 64, 8)
   instantiate_uitoa(uitoa32_hex, 32, 16)
   instantiate_uitoa(uitoa64_hex, 64, 16)

   instantiate_itoa(itoa32, 32, 10)
   instantiate_itoa(itoa64, 64, 10)
} // extern "C"
