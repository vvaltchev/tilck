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

int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);

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
#endif

#undef EXTERN

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
void uitoa32_dec(u32 value, char *destBuf);
void uitoa64_dec(u64 value, char *destBuf);

void uitoa32_oct(u32 value, char *buf);
void uitoa64_oct(u64 value, char *buf);
void uitoa32_hex(u32 value, char *buf);
void uitoa64_hex(u64 value, char *buf);
void uitoa32_hex_fixed(u32 value, char *buf);
void uitoa64_hex_fixed(u64 value, char *buf);

s32 tilck_strtol(const char *str, const char **endptr, int base, int *error);
s64 tilck_strtoll(const char *str, const char **endptr, int base, int *error);
