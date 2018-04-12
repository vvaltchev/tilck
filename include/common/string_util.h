
#pragma once

#include <common/basic_defs.h>

#if !defined(TESTING)

typedef unsigned char *va_list;
#define va_start(list, param) (list = (((va_list)&param) + sizeof(param)))
#define va_arg(list, type)    (*(type *)((list += sizeof(type)) - sizeof(type)))
#define va_end(list) // do nothing.

int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
int stricmp(const char *s1, const char *s2);
void str_reverse(char *str, size_t len);

char *strdup(const char *s);
char *const *dcopy_strarray(const char *const *argv);
void dfree_strarray(char *const *argv);

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

#if defined(__i386__) || defined(__x86_64__)
#include <common/arch/generic_x86/asm_x86_strings.h>
#endif

#else

/* Add here any necessary #include for the tests. */

#endif /* !defined(TESTING) */

void itoa32(s32 value, char *destBuf);
void itoa64(s64 value, char *destBuf);
void uitoa32_dec(u32 value, char *destBuf);
void uitoa64_dec(u64 value, char *destBuf);

void uitoa32_hex(u32 value, char *buf);
void uitoa64_hex(u64 value, char *buf);
void uitoa32_hex_fixed(u32 value, char *buf);
void uitoa64_hex_fixed(u64 value, char *buf);

void vprintk(const char *fmt, va_list args);
void printk(const char *fmt, ...);
