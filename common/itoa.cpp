
/* SPDX-License-Identifier: BSD-2-Clause */

extern "C" {

   #include <tilck/common/basic_defs.h>
   #include <tilck/common/assert.h>
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

template <typename T>
void __itoa(T svalue, char *buf, int base)
{
   typedef typename unsigned_type<T>::type U; /* same as T, if T is unsigned */
   ASSERT(IN_RANGE_INC(base, 2, 16));

   char *ptr = buf;

   if (svalue == 0) {
      *ptr++ = DIGITS[0];
      *ptr = 0;
      return;
   }

   /* If U == T, svalue >= 0 will always be true, at compile time */
   U value = svalue >= 0 ? (U) svalue : (U) -svalue;

   while (value) {
      *ptr++ = DIGITS[value % (U)base];
      value /= (U)base;
   }

   /* If U == T, svalue < 0 will always be false, at compile time */
   if (svalue < 0)
      *ptr++ = '-';

   *ptr = 0;
   str_reverse(buf, (size_t)ptr - (size_t)buf);
}

static inline bool is_valid_digit(u8 d, int base)
{
   return d < 128 && IN_RANGE(digit_to_val[d], 0, base);
}

template<typename T>
T __tilck_strtol(const char *str, const char **endptr, int base, int *error)
{
   T next, res = 0, sign = 1;
   const char *p;
   bool overflow;

   ASSERT(IN_RANGE_INC(base, 2, 16));

   if (error)
      *error = 0;

   if (!is_unsigned<T>::val) {
      if (*str == '-') {
         sign = (T) -1;
         str++;
      }
   }

   for (p = str; *p; p++) {

      u8 up = (u8)*p;

      if (!is_valid_digit(up, base))
         break;

      if (is_unsigned<T>::val) {
         next = res * (T)base + (T)digit_to_val[up];
         overflow = next < res;
      } else {
         next = res * (T)base + sign * (T)digit_to_val[up];
         overflow = (sign > 0) != (next >= 0);
      }

      if (overflow) {

         if (error)
            *error = -ERANGE;

         if (endptr)
            *endptr = str;

         return 0; // signed int overflow
      }

      res = next;
   }

   if (p == str && error)
      *error = -EINVAL;

   if (endptr)
      *endptr = p;

   return res;
}

extern "C" {

   long tilck_strtol(const char *s, const char **endptr, int base, int *err)
   {
      return __tilck_strtol<long>(s, endptr, base, err);
   }

   void uitoa32_hex_fixed(u32 value, char *buf)
   {
      __uitoa_fixed(value, buf);
   }

   void uitoa64_hex_fixed(u64 value, char *buf)
   {
      __uitoa_fixed(value, buf);
   }

   void uitoaN_hex_fixed(ulong value, char *buf)
   {
      __uitoa_fixed(value, buf);
   }

   void itoa32(s32 value, char *buf)
   {
      __itoa(value, buf, 10);
   }

   void itoa64(s64 value, char *buf)
   {
      __itoa(value, buf, 10);
   }

   void itoaN(long value, char *buf)
   {
      __itoa(value, buf, 10);
   }

   void uitoa32(u32 value, char *buf, int base)
   {
      __itoa(value, buf, base);
   }

   void uitoa64(u64 value, char *buf, int base)
   {
      __itoa(value, buf, base);
   }

   void uitoaN(ulong value, char *buf, int base)
   {
      __itoa(value, buf, base);
   }

#ifdef __TILCK_KERNEL__

   /*
    * No other reason for this #ifdef except to avoid unnecessary code-bloat
    * in the legacy bootloader.
    */

   ulong tilck_strtoul(const char *s, const char **endptr, int base, int *err) {
      return __tilck_strtol<ulong>(s, endptr, base, err);
   }

#ifdef KERNEL_TEST
   s32 tilck_strtol32(const char *s, const char **endptr, int base, int *err) {
      return __tilck_strtol<s32>(s, endptr, base, err);
   }
   u32 tilck_strtoul32(const char *s, const char **endptr, int base, int *err) {
      return __tilck_strtol<u32>(s, endptr, base, err);
   }
   s64 tilck_strtol64(const char *s, const char **endptr, int base, int *err) {
      return __tilck_strtol<s64>(s, endptr, base, err);
   }
   u64 tilck_strtoul64(const char *s, const char **endptr, int base, int *err) {
      return __tilck_strtol<u64>(s, endptr, base, err);
   }
#endif // #ifdef KERNEL_TEST
#endif // #ifdef __TILCK_KERNEL__

#if defined(__TILCK_KERNEL__) && !defined(KERNEL_TEST)

   long strtol(const char *s, char **endptr, int base) {
      return tilck_strtol(s, (const char **)endptr, base, NULL);
   }

   ulong strtoul(const char *s, char **endptr, int base) {
      return tilck_strtoul(s, (const char **)endptr, base, NULL);
   }

#endif

} // extern "C"
