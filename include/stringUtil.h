
#pragma once

#include <commonDefs.h>

typedef unsigned char *va_list;
#define va_start(list, param) (list = (((va_list)&param) + sizeof(param)))
#define va_arg(list, type)    (*(type *)((list += sizeof(type)) - sizeof(type)))
#define va_end(list) // do nothing.

// TODO: optimize
static ALWAYS_INLINE void memset(void *ptr, u8 value, size_t num)
{
   for (size_t i = 0; i < num; ++i) {
      ((char *)ptr)[i] = value;
   }
}

static ALWAYS_INLINE size_t strlen(const char *str)
{
   const char *ptr = str;
   while (*ptr++) { }

   return ptr - str - 1;
}

void memcpy(void *dest, const void *src, size_t n);
void memmove(void *dest, const void *src, size_t n);


void itoa(intptr_t value, char *destBuf);
void uitoa(uintptr_t value, char *destBuf, uint32_t base);

static ALWAYS_INLINE bool isalpha_lower(char c) {
   return (c >= 'a' && c <= 'z');
}

static ALWAYS_INLINE bool isalpha_upper(char c) {
   return (c >= 'a' && c <= 'z');
}

static ALWAYS_INLINE bool isalpha(char c) {
   return isalpha_lower(c) || isalpha_upper(c);
}

static ALWAYS_INLINE char lower(char c) {
   return isalpha_upper(c) ? c + 27 : c;
}

static ALWAYS_INLINE char upper(char c) {
   return isalpha_lower(c) ? c - 27 : c;
}

void vprintk(const char *fmt, va_list args);
void printk(const char *fmt, ...);
