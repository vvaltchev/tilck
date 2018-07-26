
#pragma once
#define _TILCK_BASIC_DEFS_H

#include <tilck/common/config.h>

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


#ifdef __cplusplus
   #include <cstdint>
   #define STATIC_ASSERT(s) static_assert(s, "Static assertion failed")
#else
   #include <stdint.h>
   #include <stddef.h>
   #include <stdbool.h>
   #define STATIC_ASSERT(s) _Static_assert(s, "Static assertion failed")
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

   #error Architecture not supported.

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
   #define WEAK
   #define NODISCARD
   #define OFFSET_OF(st, m)
   #define FASTCALL
   #define ASSUME_WITHOUT_CHECK(x)
   #define ALIGNED_AT(x)

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
   #define WEAK __attribute__((weak))
   #define PACKED __attribute__((packed))
   #define NODISCARD __attribute__((warn_unused_result))
   #define ASSUME_WITHOUT_CHECK(x) if (!(x)) __builtin_unreachable();
   #define ALIGNED_AT(x) __attribute__ ((aligned(x)))

   #ifdef BITS32
      #define FASTCALL __attribute__((fastcall))
   #else
      #define FASTCALL
   #endif

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

typedef unsigned long long ull_t;


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

#define COMPARE_AND_SWAP(ptr, oldval, newval) \
   __sync_val_compare_and_swap((ptr), (oldval), (newval))

#define BOOL_COMPARE_AND_SWAP(ptr, oldval, newval) \
   __sync_bool_compare_and_swap((ptr), (oldval), (newval))

#define ATOMIC_FETCH_AND_ADD(ptr, val) \
   __sync_fetch_and_add((ptr), (val))

#define ATOMIC_ADD_AND_FETCH(ptr, val) \
   __sync_add_and_fetch((ptr), (val))

#define ATOMIC_FETCH_AND_SUB(ptr, val) \
   __sync_fetch_and_sub((ptr), (val))

#define ATOMIC_SUB_AND_FETCH(ptr, val) \
   __sync_sub_and_fetch((ptr), (val))

#ifndef NO_TILCK_STATIC_WRAPPER

   #ifdef KERNEL_TEST
      #define STATIC
      #define STATIC_INLINE
   #else
      #define STATIC static
      #define STATIC_INLINE static inline
   #endif

#endif

bool is_tilck_known_resolution(u32 w, u32 h);
#include <tilck/common/panic.h>
