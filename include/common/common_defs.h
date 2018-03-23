
#pragma once
#include <common/config.h>

/*
 * TESTING is defined when kernel unit tests are compiled and it affects
 * actual kernel headers but NOT kernel's C files.
 *
 * KERNEL_TEST is defined when the non-arch part of the kernel is compiled for
 * unit tests. Therefore, it affects only the kernel's C files.
 *
 * UNIT_TEST_ENVIRONMENT is defined when TESTING or KERNEL_TEST is defined.
 * It make sense to be used in kernel headers that can be both used by the
 * kernel and by the tests itself, in particular when calling kernel header-only
 * code. Therefore, that macro it is the one that should be used the most since
 * it allows consistent behavior for headers & C files.
 */

#if defined(TESTING) || defined(KERNEL_TEST)
#define UNIT_TEST_ENVIRONMENT
#endif



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
#define NO_INLINE
#define typeof(x) void *

#define STATIC_ASSERT(s, err)

#define PURE
#define CONSTEXPR
#define NORETURN

#define OFFSET_OF(st, m)

#else

#ifndef TESTING
#define NORETURN _Noreturn /* C11 standard no return attribute. */
#else
#define NORETURN
#endif

#define OFFSET_OF(st, m) __builtin_offsetof(st, m)

#define ALWAYS_INLINE __attribute__((always_inline)) inline
#define NO_INLINE __attribute__((noinline))
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

typedef s8 int8_t;
typedef u8 uint8_t;
typedef s16 int16_t;
typedef u16 uint16_t;
typedef s32 int32_t;
typedef u32 uint32_t;
typedef s64 int64_t;
typedef u64 uint64_t;

#else

#ifdef __cplusplus
#include <cstdint>
#else
#include <stdint.h>
#include <stddef.h>
#endif

#endif

STATIC_ASSERT(sizeof(uptr) == sizeof(sptr));
STATIC_ASSERT(sizeof(uptr) == sizeof(void *));

#define MIN(x, y) (((x) <= (y)) ? (x) : (y))
#define MAX(x, y) (((x) > (y)) ? (x) : (y))

#define LIKELY(x) __builtin_expect((x), true)
#define UNLIKELY(x) __builtin_expect((x), false)

#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))


#define DO_NOT_OPTIMIZE_AWAY(x) asmVolatile("" : "+r" ( (void *)(x) ))


#define CONTAINER_OF(elem_ptr, struct_type, mem_name) \
   ((struct_type *)(((char *)elem_ptr) - OFFSET_OF(struct_type, mem_name)))

#define POINTER_ALIGN_MASK (~(sizeof(void *) - 1))

// Standard compare function signature among generic objects.
typedef int (*cmpfun_ptr)(const void *a, const void *b);


/*
 * ********************************************
 *
 * Panic-related stuff
 *
 * ********************************************
 */

NORETURN void panic(const char *fmt, ...);
NORETURN void assert_failed(const char *expr, const char *file, int line);
NORETURN void not_reached(const char *file, int line);

#ifndef NDEBUG

   #ifndef NO_EXOS_ASSERT

      #define ASSERT(x)                                                    \
         do {                                                              \
            if (UNLIKELY(!(x))) {                                          \
               assert_failed(#x , __FILE__, __LINE__);                     \
            }                                                              \
         } while (0)

   #endif

   #define DEBUG_ONLY(x) x
   #define DEBUG_CHECKED_SUCCESS(x)       \
      do {                                \
         bool __checked_success = x;      \
         ASSERT(__checked_success);       \
      } while (0)

#else

   #ifndef NO_EXOS_ASSERT
      #define ASSERT(x)
   #endif

   #define DEBUG_ONLY(x)
   #define DEBUG_CHECKED_SUCCESS(x) x

#endif

/* VERIFY is like ASSERT, but is enabled on release builds as well */
#define VERIFY(x)                                                    \
   do {                                                              \
      if (UNLIKELY(!(x))) {                                          \
         assert_failed(#x , __FILE__, __LINE__);                     \
      }                                                              \
   } while (0)


#define NOT_REACHED() not_reached(__FILE__, __LINE__)
#define NOT_IMPLEMENTED() panic("Code path not implemented yet.")

#ifndef NO_EXOS_STATIC_WRAPPER

   #ifdef KERNEL_TEST
      #define STATIC
      #define STATIC_INLINE
   #else
      #define STATIC static
      #define STATIC_INLINE static inline
   #endif

#endif
