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

char *tilck_strstr(const char *haystack, const char *needle)
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

#if !defined(TESTING) && !defined(USERMODE_APP)
   char *strstr(const char *haystack, const char *needle) \
   __attribute__((alias("tilck_strstr")));
#endif


char *strcpy(char *dest, const char *src)
{
   char *p = dest;

   while (*src)
      *p++ = *src++;

   *p = 0;
   return dest;
}

char *tilck_strncpy(char *dest, const char *src, size_t n)
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

#if !defined(TESTING) && !defined(USERMODE_APP)
   char *strncpy(char *dest, const char *src, size_t n) \
   __attribute__((alias("tilck_strncpy")));
#endif

char *strcat(char *dest, const char *src)
{
   return strcpy(dest + strlen(dest), src);
}

char *tilck_strncat(char *dest, const char *src, size_t n)
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

#if !defined(TESTING) && !defined(USERMODE_APP)
   char *strncat(char *dest, const char *src, size_t n) \
   __attribute__((alias("tilck_strncat")));
#endif

int tilck_isxdigit(int c)
{
   return c < 128 && digit_to_val[c] >= 0;
}

#if !defined(TESTING) && !defined(USERMODE_APP)
   int isxdigit(int c) __attribute__((alias("tilck_isxdigit")));
#endif

int tilck_isspace(int c)
{
   return c == ' ' || c == '\t' || c == '\r' ||
          c == '\n' || c == '\v' || c == '\f';
}

#if !defined(TESTING) && !defined(USERMODE_APP)
   int isspace(int c) __attribute__((alias("tilck_isspace")));
#endif

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

#if (defined(__aarch64__) && defined(KERNEL_TEST)) || defined(__riscv)

void *memset16(u16 *s, u16 val, size_t n)
{
   for (size_t i = 0; i < n; i++)
      s[i] = val;

   return s;
}

void *memset32(u32 *s, u32 val, size_t n)
{
   for (size_t i = 0; i < n; i++)
      s[i] = val;

   return s;
}

void *memcpy16(void *dest, const void *src, size_t n)
{
   return memcpy(dest, src, n * 2);
}

void *memcpy32(void *dest, const void *src, size_t n)
{
   return memcpy(dest, src, n * 4);
}


size_t strlen(const char *s)
{
   const char *sc;

   for (sc = s; *sc != '\0'; ++sc)
      /* nothing */;
   return sc - s;
}

/* dest and src can overloap only partially */
void *memcpy(void *dest, const void *src, size_t count)
{
   char *tmp = (char *)dest;
   const char *s = (char *)src;

   while (count--)
      *tmp++ = *s++;
   return dest;
}

/* dest and src might overlap anyhow */
void *memmove(void *dest, const void *src, size_t count)
{
   char *tmp;
   const char *s;

   if (dest <= src) {
      tmp = (char *)dest;
      s = (const char *)src;
      while (count--)
         *tmp++ = *s++;
   } else {
      tmp = (char *)dest;
      tmp += count;
      s = (const char *)src;
      s += count;
      while (count--)
         *--tmp = *--s;
   }
   return dest;

}

/*
 * Set 'n' bytes pointed by 's' to 'c'.
 */
void *memset(void *s, int c, size_t count)
{
   char *xs = (char *)s;

   while (count--)
      *xs++ = c;
   return s;
}

void bzero(void *s, size_t n)
{
   memset(s, 0, n);
}

size_t strnlen(const char *str, size_t count)
{
   unsigned long ret = 0;

   while (*str != '\0' && ret < count) {
      ret++;
      str++;
   }

   return ret;
}

void *memchr(const void *s, int c, size_t count)
{
   const unsigned char *temp = (const unsigned char *)s;

   while (count > 0) {
      if ((unsigned char)c == *temp++) {
         return (void *)(temp - 1);
      }
      count--;
   }

   return NULL;
}

char *strrchr(const char *s, int c)
{
   const char *last = s + strlen(s);

   while (last > s && *last != (char)c)
      last--;

   if (*last != (char)c)
      return NULL;
   else
      return (char *)last;
}

char *strchr(const char *s, int c)
{
   for (; *s != (char)c; ++s)
      if (*s == '\0')
         return NULL;
   return (char *)s;
}

#endif
