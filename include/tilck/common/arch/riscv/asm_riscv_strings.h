/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

/*
 * TODO: Implement arch-specific version string function
 * instead of the generic C version function
 */
size_t strlen(const char *s);
void *memcpy(void *dest, const void *src, size_t count);
void *memmove(void *dest, const void *src, size_t count);
void *memset(void *s, int c, size_t count);
void bzero(void *s, size_t n);
size_t strnlen(const char *str, size_t count);
void *memchr(const void *s, int c, size_t count);
char *strrchr(const char *s, int c);
char *strchr(const char *s, int c);
void *memset16(u16 *s, u16 val, size_t n);
void *memset32(u32 *s, u32 val, size_t n);
void *memcpy16(void *dest, const void *src, size_t n);
void *memcpy32(void *dest, const void *src, size_t n);
