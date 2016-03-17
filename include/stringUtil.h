
#pragma once

#include <commonDefs.h>

typedef unsigned char *va_list;
#define va_start(list, param) (list = (((va_list)&param) + sizeof(param)))
#define va_arg(list, type)    (*(type *)((list += sizeof(type)) - sizeof(type)))

void *memset(void *ptr, int value, size_t num);
void *memcpy(void *dest, void *src, size_t num);
size_t strlen(const char *str);

void itoa(int value, char *destBuf);
void uitoa(unsigned value, char *destBuf, unsigned base);


void printk(const char *fmt, ...);

