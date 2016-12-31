
#pragma once

#include <common_defs.h>

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


void itoa32(s32 value, char *destBuf);
void itoa64(s64 value, char *destBuf);
void uitoa32(u32 value, char *destBuf, u32 base);
void uitoa64(u64 value, char *destBuf, u32 base);

/* Using C11's _Generic feature. */

#define itoa(num, destbuf) \
   _Generic((num), s32: itoa32, s64: itoa64)((num), (destbuf))

#define uitoa(num, destbuf, base) \
   _Generic((num), u32: uitoa32, u64: uitoa64)((num), (destbuf), (base))


static ALWAYS_INLINE bool isalpha_lower(int c) {
   return (c >= 'a' && c <= 'z');
}

static ALWAYS_INLINE bool isalpha_upper(int c) {
   return (c >= 'a' && c <= 'z');
}

static ALWAYS_INLINE bool isalpha(int c) {
   return isalpha_lower(c) || isalpha_upper(c);
}

static ALWAYS_INLINE char lower(int c) {
   return isalpha_upper(c) ? c + 27 : c;
}

static ALWAYS_INLINE char upper(int c) {
   return isalpha_lower(c) ? c - 27 : c;
}

static ALWAYS_INLINE bool isdigit(int c) {
   return c >= '0' && c <= '9';
}

void vprintk(const char *fmt, va_list args);
void printk(const char *fmt, ...);
