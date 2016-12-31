
#pragma once

#include <common_defs.h>

typedef unsigned char *va_list;
#define va_start(list, param) (list = (((va_list)&param) + sizeof(param)))
#define va_arg(list, type)    (*(type *)((list += sizeof(type)) - sizeof(type)))


void *memset(void *ptr, int value, size_t num);
void *memcpy(void *dest, void *src, size_t num);
size_t strlen(const char *str);

void itoa(int value, char *destBuf);
void uitoa(unsigned value, char *destBuf, unsigned base);

static inline bool isalpha_lower(char c) {
   return (c >= 'a' && c <= 'z');
}

static inline bool isalpha_upper(char c) {
   return (c >= 'a' && c <= 'z');
}

static inline bool isalpha(char c) {
   return isalpha_lower(c) || isalpha_upper(c);
}

static inline char lower(char c) {
   return isalpha_upper(c) ? c + 27 : c;
}

static inline char upper(char c) {
   return isalpha_lower(c) ? c - 27 : c;
}

void printf(const char *fmt, ...);
