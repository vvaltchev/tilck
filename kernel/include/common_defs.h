
#pragma once

#ifndef __cplusplus

typedef _Bool bool;
#define true ((bool)1)
#define false ((bool)0)
#define STATIC_ASSERT(s) _Static_assert(s, "Static assertion failed")

#else

#define STATIC_ASSERT(s) static_assert(s, "Static assertion failed")

#endif

#ifdef __i386__

STATIC_ASSERT(sizeof(void *) == 4);
STATIC_ASSERT(sizeof(long) == sizeof(void *));

#define BITS32

#elif defined(__x86_64__)

STATIC_ASSERT(sizeof(void *) == 8);
STATIC_ASSERT(sizeof(long) == sizeof(void *));

#define BITS64

#else

#error Platform not supported.

#endif

#ifdef _MSC_VER

#define inline __inline
#define __i386__

// Make the Microsoft Intellisense happy: this does not have to work
// since we use GCC. Just, we'd like to avoid to confuse intellisense.
#define asmVolatile(...)
#define __attribute__(...)

#define asm(...)

#define ALWAYS_INLINE inline
#define typeof(x) void *

#define STATIC_ASSERT(s, err)

#define PURE
#define CONSTEXPR
#define NORETURN

#define OFFSET_OF(st, m)

#else

#define OFFSET_OF(st, m) __builtin_offsetof(st, m)

#define NORETURN _Noreturn /* C11 standard no return attribute. */
#define ALWAYS_INLINE __attribute__((always_inline)) inline

#define asmVolatile __asm__ volatile
#define asm __asm__

#define typeof(x) __typeof__(x)

#define PURE __attribute__((pure))
#define CONSTEXPR __attribute__((const))

#endif

typedef char s8;
typedef short s16;
typedef int s32;
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#ifdef BITS32
typedef unsigned long long u64;
typedef long long s64;
typedef u32 uptr;
typedef s32 sptr;
#else
typedef unsigned long u64;
typedef long s64;
typedef u64 uptr;
typedef s64 sptr;
#endif

#ifndef TESTING

typedef long ssize_t; // signed pointer-size integer
typedef unsigned long size_t; // unsigned pointer-size integer
typedef ssize_t ptrdiff_t;
#define NULL ((void *) 0)

#else

#include <cstdint>

#endif

STATIC_ASSERT(sizeof(uptr) == sizeof(sptr));
STATIC_ASSERT(sizeof(uptr) == sizeof(void *));

// Used to break with the Bochs x86 emulator.
#define magic_debug_break() asmVolatile("xchg %bx, %bx")

#define KB (1U << 10)
#define MB (1U << 20)

#define MIN(x, y) (((x) <= (y)) ? (x) : (y))
#define MAX(x, y) (((x) > (y)) ? (x) : (y))

#define LIKELY(x) __builtin_expect((x), true)
#define UNLIKELY(x) __builtin_expect((x), false)

#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))


#define DO_NOT_OPTIMIZE_AWAY(x) asmVolatile("" : "+r" ( (void *)(x) ))


// Putting reboot() temporarly here.
void reboot();


/*
 * Invalidates the TLB entry used for resolving the page containing 'vaddr'.
 */
static ALWAYS_INLINE void invalidate_page(uptr vaddr)
{
   asmVolatile("invlpg (%0)" ::"r" (vaddr) : "memory");
}


#define CONTAINER_OF(elem_ptr, struct_type, mem_name) \
   ((struct_type *)(((char *)elem_ptr) - OFFSET_OF(struct_type, mem_name)))


/*
 * ********************************************
 *
 * Panic-related stuff
 *
 * ********************************************
 */

void panic(const char *fmt, ...);
void assert_failed(const char *expr, const char *file, int line);
void not_reached(const char *file, int line);

#ifndef NDEBUG

#define ASSERT(x)                                                    \
   do {                                                              \
      if (UNLIKELY(!(x))) {                                          \
         assert_failed(#x , __FILE__, __LINE__);                     \
      }                                                              \
   } while (0)

#else

#define ASSERT(x) (void)(x)

#endif


#define NOT_REACHED() not_reached(__FILE__, __LINE__)
