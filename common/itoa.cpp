
/* SPDX-License-Identifier: BSD-2-Clause */

extern "C" {

   #include <tilck/common/basic_defs.h>
   #include <tilck/common/string_util.h>
   #include <tilck/kernel/errno.h>

   extern const s8 digit_to_val[128];
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
   STATIC_ASSERT(IN_RANGE_INC(base, 2, 16));
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
   STATIC_ASSERT(IN_RANGE_INC(base, 2, 16));
   typedef typename unsigned_type<T>::type U;

   char *ptr = buf;

   if (svalue == 0) {
      *ptr++ = DIGITS[0];
      *ptr = 0;
      return;
   }

   U value = svalue >= 0 ? (U) svalue : (U) -svalue;

   while (value) {
      *ptr++ = DIGITS[value % base];
      value /= base;
   }

   if (svalue < 0)
      *ptr++ = '-';

   *ptr = 0;
   str_reverse(buf, (size_t)ptr - (size_t)buf);
}


template<typename T>
T __tilck_strtol(const char *str, const char **endptr, int base, int *error)
{
   T res = 0;
   T sign = 1;
   const char *p;
   ASSERT(IN_RANGE_INC(base, 2, 16));

   if (*str == '-') {
      sign = -1;
      str++;
   }

   for (p = str; *p; p++) {

      u8 up = (u8)*p;

      if (up >= 128)
         break;

      if (digit_to_val[up] < 0)
         break;

      if (digit_to_val[up] >= base)
         break;

      res = res * base + sign * digit_to_val[up];

      if ((sign > 0) != (res >= 0)) {

         if (error)
            *error = -ERANGE;

         if (endptr)
            *endptr = str;

         return 0; // signed int overflow
      }
   }

   if (p == str && error)
      *error = -EINVAL;

   if (endptr)
      *endptr = p;

   return res;
}

template<typename T>
T __tilck_strtoul(const char *str, const char **endptr, int base, int *error)
{
   T next, res = 0;
   const char *p;
   ASSERT(IN_RANGE_INC(base, 2, 16));

   for (p = str; *p; p++) {

      u8 up = (u8)*p;

      if (up >= 128)
         break;

      if (digit_to_val[up] < 0)
         break;

      if (digit_to_val[up] >= base)
         break;

      next = res * (T)base + (T)digit_to_val[up];

      if (next < res) {

         if (error)
            *error = -ERANGE;

         if (endptr)
            *endptr = str;

         return 0; // overflow
      }

      res = next;
   }

   if (p == str && error)
      *error = -EINVAL;

   if (endptr)
      *endptr = p;

   return res;
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

   s32 tilck_strtol(const char *s, const char **endptr, int base, int *err) {
      return __tilck_strtol<s32>(s, endptr, base, err);
   }

#ifdef __TILCK_KERNEL__

   /*
    * No other reason for this #ifdef except to avoid unnecessary code-bloat
    * in the legacy bootloader.
    */

   s64 tilck_strtoll(const char *s, const char **endptr, int base, int *err) {
      return __tilck_strtol<s64>(s, endptr, base, err);
   }

   u32 tilck_strtoul(const char *s, const char **endptr, int base, int *err) {
      return __tilck_strtoul<u32>(s, endptr, base, err);
   }

   u64 tilck_strtoull(const char *s, const char **endptr, int base, int *err) {
      return __tilck_strtoul<u64>(s, endptr, base, err);
   }

#endif

#if defined(__TILCK_KERNEL__) && !defined(KERNEL_TEST)

   long strtol(const char *s, const char **endptr, int base) {
      return NBITS == 32
         ? tilck_strtol(s, endptr, base, NULL)
         : tilck_strtoll(s, endptr, base, NULL);
   }

   ulong strtoul(const char *s, const char **endptr, int base) {
      return NBITS == 32
         ? tilck_strtoul(s, endptr, base, NULL)
         : tilck_strtoull(s, endptr, base, NULL);
   }

#endif

} // extern "C"
