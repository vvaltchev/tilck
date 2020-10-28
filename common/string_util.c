/* SPDX-License-Identifier: BSD-2-Clause */

#define __STRING_UTIL_C__

#include <tilck/common/basic_defs.h>
#include <tilck/common/assert.h>
#include <tilck/common/string_util.h>
#include <tilck/kernel/errno.h>

const s8 digit_to_val[128] =
{
   [0 ... 47] = -1,

   [48] = 0,         /* '0' */
   [49] = 1,         /* '1' */
   [50] = 2,         /* '2' */
   [51] = 3,         /* '3' */
   [52] = 4,         /* '4' */
   [53] = 5,         /* '5' */
   [54] = 6,         /* '6' */
   [55] = 7,         /* '7' */
   [56] = 8,         /* '7' */
   [57] = 9,         /* '7' */

   [58 ... 64] = -1,

   [65] = 10,        /* 'A' */
   [66] = 11,        /* 'B' */
   [67] = 12,        /* 'C' */
   [68] = 13,        /* 'D' */
   [69] = 14,        /* 'E' */
   [70] = 15,        /* 'F' */

   [71 ... 96] = -1,

   [ 97] = 10,        /* 'a' */
   [ 98] = 11,        /* 'b' */
   [ 99] = 12,        /* 'c' */
   [100] = 13,        /* 'd' */
   [101] = 14,        /* 'e' */
   [102] = 15,        /* 'f' */

   [103 ... 127] = -1,
};

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

int memcmp(const void *_m1, const void *_m2, size_t n)
{
   size_t i = 0;
   const char *m1 = _m1;
   const char *m2 = _m2;

   while(i < n && *m1 == *m2) {
      m1++; m2++; i++;
   }

   return i == n ? 0 : (int)*m1 - (int)*m2;
}

char *strstr(const char *haystack, const char *needle)
{
   size_t sl, nl;

   if (!*haystack || !*needle)
      return NULL;

   sl = strlen(haystack);
   nl = strlen(needle);

   while (*haystack && sl >= nl) {

      if (*haystack == *needle && !strncmp(haystack, needle, nl))
         return (char *)haystack;

      haystack++;
      sl--;
   }

   return NULL;
}

char *strcpy(char *dest, const char *src)
{
   char *p = dest;

   while (*src)
      *p++ = *src++;

   *p = 0;
   return dest;
}

char *strncpy(char *dest, const char *src, size_t n)
{
   char *p = dest;
   size_t i = 0;

   while (*src && i < n) {
      *p++ = *src++;
      i++;
   }

   if (i < n)
      *p = 0;

   return dest;
}

char *strcat(char *dest, const char *src)
{
   return strcpy(dest + strlen(dest), src);
}

char *strncat(char *dest, const char *src, size_t n)
{
   char *p = dest + strlen(dest);
   size_t i = 0;

   while (*src && i < n) {
      *p++ = *src++;
      i++;
   }

   *p = 0;
   return dest;
}


int isxdigit(int c)
{
   return c < 128 && digit_to_val[c] >= 0;
}

int isspace(int c)
{
   return c == ' ' || c == '\t' || c == '\r' ||
          c == '\n' || c == '\v' || c == '\f';
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
