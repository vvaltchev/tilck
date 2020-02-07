/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

#if !defined(TESTING) && !defined(USERMODE_APP)

int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);

char *strdup(const char *s);

static ALWAYS_INLINE bool isalpha_lower(int c) {
   return IN_RANGE_INC(c, 'a', 'z');
}

static ALWAYS_INLINE bool isalpha_upper(int c) {
   return IN_RANGE_INC(c, 'A', 'Z');
}

static ALWAYS_INLINE int isalpha(int c) {
   return isalpha_lower(c) || isalpha_upper(c);
}

static ALWAYS_INLINE int tolower(int c) {
   return isalpha_upper(c) ? c + 32 : c;
}

static ALWAYS_INLINE int toupper(int c) {
   return isalpha_lower(c) ? c - 32 : c;
}

static ALWAYS_INLINE int isdigit(int c) {
   return IN_RANGE_INC(c, '0', '9');
}

static ALWAYS_INLINE int isprint(int c) {
   return IN_RANGE_INC(c, ' ', '~');
}

#if defined(__i386__) || defined(__x86_64__)
   #include <tilck/common/arch/generic_x86/asm_x86_strings.h>
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
char *const *dup_strarray(const char *const *argv);
void free_strarray(char *const *argv);

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

int tilck_strtol(const char *str, const char **endptr, int *error);
