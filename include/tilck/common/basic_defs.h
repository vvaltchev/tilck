/* SPDX-License-Identifier: BSD-2-Clause */

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
   #include <cstdint>     // system header
   #define STATIC_ASSERT(s) static_assert(s, "Static assertion failed")
#else
   #include <stdint.h>    // system header
   #include <stddef.h>    // system header
   #include <stdbool.h>   // system header
   #include <stdalign.h>  // system header
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

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#ifdef BITS32
   typedef s32 sptr;
   typedef u32 uptr;
#else
   typedef s64 sptr;
   typedef u64 uptr;
#endif

typedef unsigned long long ull_t;


STATIC_ASSERT(sizeof(uptr) == sizeof(sptr));
STATIC_ASSERT(sizeof(uptr) == sizeof(void *));

/*
 * An useful two-pass concatenation macro.
 *
 * The reason for using a two-pass macro is to allow the arguments to expand
 * in case they are using macros themselfs. Consider the following example:
 *
 *    #define SOME_STRING_LITERAL "hello world"
 *    #define WIDE_STR_LITERAL _CONCAT(L, SOME_STRING_LITERAL)
 *
 * The macro `WIDE_STR_LITERAL` will expand to: LSOME_STRING_LITERAL. That
 * clearly is NOT what we wanted. While, by using the two-pass expansion we
 * get `WIDE_STR_LITERAL` expanded to: L"hello world".
 */
#define _CONCAT(a, b) a##b
#define CONCAT(a, b) _CONCAT(a, b)

/*
 * UNSAFE against double-evaluation MIN and MAX macros.
 * They are necessary for all the cases when the compiler (GCC and Clang)
 * fails to compile with the other ones. The known cases are:
 *
 *    - Initialization of struct fields like:
 *          struct x var = (x) { .field1 = MIN(a, b), .field2 = 0 };
 *
 *    - Use bit-field variable as argument of MIN() or MAX()
 *
 * There might be other cases as well.
 */
#define UNSAFE_MIN(x, y) (((x) <= (y)) ? (x) : (y))
#define UNSAFE_MAX(x, y) (((x) > (y)) ? (x) : (y))

#define UNSAFE_MIN3(x, y, z) UNSAFE_MIN(UNSAFE_MIN((x), (y)), (z))
#define UNSAFE_MAX3(x, y, z) UNSAFE_MAX(UNSAFE_MAX((x), (y)), (z))

/*
 * SAFE against double-evaluation MIN and MAX macros.
 * Use these when possible. In all the other cases, use their UNSAFE version.
 */
#define MIN(a, b)                                                     \
   ({                                                                 \
      const typeof(a) CONCAT(_a, __LINE__) = (a);                     \
      const typeof(b) CONCAT(_b, __LINE__) = (b);                     \
      UNSAFE_MIN(CONCAT(_a, __LINE__), CONCAT(_b, __LINE__));         \
   })

#define MAX(a, b) \
   ({                                                                 \
      const typeof(a) CONCAT(_a, __LINE__) = (a);                     \
      const typeof(b) CONCAT(_b, __LINE__) = (b);                     \
      UNSAFE_MAX(CONCAT(_a, __LINE__), CONCAT(_b, __LINE__));         \
   })

#define MIN3(a, b, c)                                                 \
   ({                                                                 \
      const typeof(a) CONCAT(_a, __LINE__) = (a);                     \
      const typeof(b) CONCAT(_b, __LINE__) = (b);                     \
      const typeof(c) CONCAT(_c, __LINE__) = (c);                     \
      UNSAFE_MIN3(CONCAT(_a, __LINE__),                               \
                  CONCAT(_b, __LINE__),                               \
                  CONCAT(_c, __LINE__));                              \
   })

#define MAX3(a, b, c)                                                 \
   ({                                                                 \
      const typeof(a) CONCAT(_a, __LINE__) = (a);                     \
      const typeof(b) CONCAT(_b, __LINE__) = (b);                     \
      const typeof(c) CONCAT(_c, __LINE__) = (c);                     \
      UNSAFE_MAX3(CONCAT(_a, __LINE__),                               \
                  CONCAT(_b, __LINE__),                               \
                  CONCAT(_c, __LINE__));                              \
   })

#define UNSAFE_BOUND(val, minval, maxval)                             \
   UNSAFE_MIN(UNSAFE_MAX((val), (minval)), (maxval))

#define BOUND(val, minval, maxval)                                    \
   ({                                                                 \
      const typeof(val) CONCAT(_v, __LINE__) = (val);                 \
      const typeof(minval) CONCAT(_mv, __LINE__) = (minval);          \
      const typeof(maxval) CONCAT(_Mv, __LINE__) = (maxval);          \
      UNSAFE_BOUND(CONCAT(_v, __LINE__),                              \
                   CONCAT(_mv, __LINE__),                             \
                   CONCAT(_Mv, __LINE__));                            \
   })

#define LIKELY(x) __builtin_expect((x), true)
#define UNLIKELY(x) __builtin_expect((x), false)

#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define CONTAINER_OF(elem_ptr, struct_type, mem_name) \
   ((struct_type *)(((char *)elem_ptr) - OFFSET_OF(struct_type, mem_name)))

#ifndef __clang__

   #define DO_NOT_OPTIMIZE_AWAY(x) asmVolatile("" : "+r" ( (void *)(x) ))

#else

   static ALWAYS_INLINE void __do_not_opt_away(void *x)
   {
      asmVolatile("" ::: "memory");
   }

   #define DO_NOT_OPTIMIZE_AWAY(x) (__do_not_opt_away((void *)(x)))

#endif

#define POINTER_ALIGN_MASK (~(sizeof(void *) - 1))

// Standard compare function signature among generic objects.
typedef int (*cmpfun_ptr)(const void *a, const void *b);

#ifndef NO_TILCK_STATIC_WRAPPER

   /*
    * The whole point of having the following STATIC* macros is to allow unit
    * tests to wrap the functions using them, while keeping the symbols really
    * static when the actual kernel is built (preventing other translation units
    * to use them). In particular, it is important to mark those symbols as
    * WEAK as well because the linker-level --wrap does not work when both the
    * caller and the callee are in the same translation unit. With weak symbols
    * instead, unit tests can just re-define those symbols without using any
    * kind of additional linker tricks.
    */

   #ifdef KERNEL_TEST
      #define STATIC           WEAK
      #define STATIC_INLINE    WEAK
   #else
      #define STATIC           static
      #define STATIC_INLINE    static inline
   #endif

#endif

#include <tilck/common/panic.h>
