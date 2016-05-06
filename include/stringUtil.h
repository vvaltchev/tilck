
#pragma once

#include <commonDefs.h>

typedef unsigned char *va_list;
#define va_start(list, param) (list = (((va_list)&param) + sizeof(param)))
#define va_arg(list, type)    (*(type *)((list += sizeof(type)) - sizeof(type)))
#define va_end(list) // do nothing.

// TODO: optimize
static ALWAYS_INLINE void memset(volatile void *ptr, int value, size_t num)
{
   for (size_t i = 0; i < num; ++i)
      ((char*)ptr)[i] = value;
}

// TODO: optimize
static ALWAYS_INLINE size_t strlen(const char *str)
{
   size_t c = 0;
   while (*str++) { ++c; }
   return c;
}


// Dest and src can overlap
// TODO: optimize
static ALWAYS_INLINE void memmove(volatile void *dest, volatile void *src, size_t num)
{
   volatile char *dst = (volatile char *)dest;
   volatile char *s = (volatile char *)src;

   for (size_t i = 0; i < num; i++) {
      *dst++ = *s++;
   }
}

// Dest and src cannot overlap
// TODO: optimize
static ALWAYS_INLINE void memcpy(volatile void *dest, volatile void *src, size_t num)
{
   memmove(dest, src, num);
}

void itoa(int value, char *destBuf);
void uitoa(uint32_t value, char *destBuf, uint32_t base);

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
