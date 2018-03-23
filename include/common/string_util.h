
#pragma once

#include <common/basic_defs.h>

#if !defined(TESTING)

typedef unsigned char *va_list;
#define va_start(list, param) (list = (((va_list)&param) + sizeof(param)))
#define va_arg(list, type)    (*(type *)((list += sizeof(type)) - sizeof(type)))
#define va_end(list) // do nothing.

static ALWAYS_INLINE void bzero(void *ptr, size_t len)
{
   const size_t len4 = len >> 2;
   for (size_t i = 0; i < len4; i++)
      ((u32 *)ptr)[i] = 0;

   len = len % 4;
   ptr = ((u8 *)ptr) + (len4 << 2);

   for (u32 i = 0; i < len; i++)
      ((u8 *)ptr)[i] = 0;
}

static ALWAYS_INLINE size_t strlen(const char *str)
{
   const char *ptr = str;
   while (*ptr++) { }
   return ptr - str - 1;
}

int strcmp(const char *s1, const char *s2);
int stricmp(const char *s1, const char *s2);
void memcpy(void *dest, const void *src, size_t n);
void memmove(void *dest, const void *src, size_t n);
void str_reverse(char *str, size_t len);

char *strdup(const char *s);
char *const *dcopy_strarray(const char *const *argv);
void dfree_strarray(char *const *argv);

void itoa32(s32 value, char *destBuf);
void itoa64(s64 value, char *destBuf);
void uitoa32(u32 value, char *destBuf, u32 base);
void uitoa64(u64 value, char *destBuf, u32 base);

static ALWAYS_INLINE bool isalpha_lower(int c) {
   return (c >= 'a' && c <= 'z');
}

static ALWAYS_INLINE bool isalpha_upper(int c) {
   return (c >= 'A' && c <= 'Z');
}

static ALWAYS_INLINE bool isalpha(int c) {
   return isalpha_lower(c) || isalpha_upper(c);
}

static ALWAYS_INLINE char lower(int c) {
   return isalpha_upper(c) ? c + 32 : c;
}

static ALWAYS_INLINE char upper(int c) {
   return isalpha_lower(c) ? c - 32 : c;
}

static ALWAYS_INLINE bool isdigit(int c) {
   return c >= '0' && c <= '9';
}

#else

/* Add here any necessary #include for the tests. */

#endif

void vprintk(const char *fmt, va_list args);
void printk(const char *fmt, ...);
