/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

#if !defined(TESTING) && !defined(USERMODE_APP)

#ifdef STATIC_TILCK_ASM_STRING

   /* Some targets demand those symbols to be static to avoid link conflicts */
   #define EXTERN static

#else

   /*
    * This nice trick allows the code for the following functions to be emitted,
    * when not inlined, in only one translation unit, the one that declare them
    * as "extern". This is a little better than just using static inline because
    * avoids code duplication when the compiler decide to not inline a given
    * function. Compared to using static + ALWAYS_INLINE this gives the compiler
    * the maximum freedom to optimize.
    */

   #ifdef __STRING_UTIL_C__
      #define EXTERN extern
   #else
      #define EXTERN
   #endif

#endif

#if !(defined(__aarch64__) && defined(KERNEL_TEST))

/*
 * Don't define this function in this special case, because we're going to
 * include <string.h>.
 */

int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
int memcmp(const void *m1, const void *m2, size_t n);
char *strstr(const char *haystack, const char *needle);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t n);

int isxdigit(int c);
int isspace(int c);

#endif

EXTERN inline bool isalpha_lower(int c) {
   return IN_RANGE_INC(c, 'a', 'z');
}

EXTERN inline bool isalpha_upper(int c) {
   return IN_RANGE_INC(c, 'A', 'Z');
}

EXTERN inline int isalpha(int c) {
   return isalpha_lower(c) || isalpha_upper(c);
}

EXTERN inline int tolower(int c) {
   return isalpha_upper(c) ? c + 32 : c;
}

EXTERN inline int toupper(int c) {
   return isalpha_lower(c) ? c - 32 : c;
}

EXTERN inline int isdigit(int c) {
   return IN_RANGE_INC(c, '0', '9');
}

EXTERN inline int isprint(int c) {
   return IN_RANGE_INC(c, ' ', '~');
}

#if defined(__i386__) || defined(__x86_64__)

   #include <tilck/common/arch/generic_x86/asm_x86_strings.h>

#elif defined(__riscv)

   #include <tilck/common/arch/riscv/asm_riscv_strings.h>

#elif (defined(__aarch64__) && defined(KERNEL_TEST))

   /*
    * The Tilck kernel has no support for AARCH64 per se, but its noarch
    * code can be compiled as part of the unit tests on an AARCH64 host.
    */

   #include <string.h>
   void *memset16(u16 *s, u16 val, size_t n);
   void *memset32(u32 *s, u32 val, size_t n);
   void *memcpy16(void *dest, const void *src, size_t n);
   void *memcpy32(void *dest, const void *src, size_t n);

#endif

#undef EXTERN

#ifdef __MOD_ACPICA__
   long strtol(const char *__restrict, char **__restrict, int base);
   ulong strtoul(const char *__restrict, char **__restrict, int base);
#endif

#else

#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#endif // #if !defined(TESTING) && !defined(USERMODE_APP)

static ALWAYS_INLINE bool slash_or_nul(char c) {
   return !c || c == '/';
}

static inline bool is_dot_or_dotdot(const char *n, int nl) {
   return (n[0] == '.' && (nl == 1 || (n[1] == '.' && nl == 2)));
}

int stricmp(const char *s1, const char *s2);
void str_reverse(char *str, size_t len);

void itoa32(s32 value, char *destBuf);
void itoa64(s64 value, char *destBuf);
void itoaN(long value, char *buf);                /* pointer-size */

void uitoa32(u32 value, char *buf, int base);
void uitoa64(u64 value, char *buf, int base);
void uitoaN(ulong value, char *buf, int base);    /* pointer-size */

void uitoa32_hex_fixed(u32 value, char *buf);
void uitoa64_hex_fixed(u64 value, char *buf);
void uitoaN_hex_fixed(ulong value, char *buf);    /* pointer-size */

long tilck_strtol(const char *str, const char **endptr, int base, int *error);
ulong tilck_strtoul(const char *str, const char **endptr, int base, int *error);
int tilck_strcmp(const char *s1, const char *s2);
int tilck_strncmp(const char *s1, const char *s2, size_t n);
int tilck_memcmp(const void *m1, const void *m2, size_t n);
char *tilck_strstr(const char *haystack, const char *needle);
char *tilck_strcpy(char *dest, const char *src);
char *tilck_strncpy(char *dest, const char *src, size_t n);
char *tilck_strcat(char *dest, const char *src);
char *tilck_strncat(char *dest, const char *src, size_t n);
int tilck_isxdigit(int c);
int tilck_isspace(int c);
