/* SPDX-License-Identifier: BSD-2-Clause */

#define __STRING_UTIL_C__

#include <tilck/common/basic_defs.h>
#include <tilck/common/failsafe_assert.h>
#include <tilck/common/string_util.h>
#include <tilck/kernel/errno.h>

/* Compile-in strcmp() and strncmp() only when there's no libc */
#if !defined(TESTING) && !defined(USERMODE_APP)

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

#endif // #if !defined(TESTING) && !defined(USERMODE_APP)

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

int tilck_strtol(const char *str, const char **endptr, int *error)
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
         break;

      res = res * 10 + sign * (*p - '0');

      if ((sign > 0) != (res > 0)) {

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
