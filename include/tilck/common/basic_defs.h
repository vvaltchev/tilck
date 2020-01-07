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
   #define NBITS 32

#elif defined(__x86_64__)

   STATIC_ASSERT(sizeof(void *) == 8);
   STATIC_ASSERT(sizeof(long) == sizeof(void *));

   #define BITS64
   #define NBITS 64

#else

   #error Architecture not supported.

#endif

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

/*
 * Tilck's off_t, which does not depend on any extern include files and it's
 * pointer-size wide.
 */
typedef sptr offt;


STATIC_ASSERT(sizeof(uptr) == sizeof(sptr));
STATIC_ASSERT(sizeof(uptr) == sizeof(void *));

#if !defined(TESTING) && !defined(USERMODE_APP)

   /* va_list types and defs */
   typedef __builtin_va_list va_list;

   #define va_start(v,l)         __builtin_va_start(v,l)
   #define va_end(v)             __builtin_va_end(v)
   #define va_arg(v,l)           __builtin_va_arg(v,l)

#else

   #include <stdarg.h>

#endif

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

#define CLAMP(val, minval, maxval)                                    \
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
typedef sptr (*cmpfun_ptr)(const void *a, const void *b);

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

   #ifdef UNIT_TEST_ENVIRONMENT
      #define STATIC           WEAK
      #define STATIC_INLINE    WEAK
   #else
      #define STATIC           static
      #define STATIC_INLINE    static inline
   #endif

#endif

/*
 * Macros and inline functions designed to minimize the ugly code necessary
 * if we want to compile with -Wconversion.
 */

#define U32_BITMASK(n) ((u32)((1u << (n)) - 1u))
#define U64_BITMASK(n) ((u64)((1ull << (n)) - 1u))

/*
 * Get the lower `n` bits from val.
 *
 * Use case:
 *
 *    union { u32 a: 20; b: 12 } u;
 *    u32 var = 123;
 *    u.a = var; // does NOT compile with -Wconversion
 *    u.a = LO_BITS(var, 20, u32); // always compiles
 *
 * NOTE: Tilck support only clang's -Wconversion, not GCC's.
 */

#if defined(BITS64)
   #define LO_BITS(val, n, t) ((t)((val) & U64_BITMASK(n)))
#elif defined(BITS32)
   #define LO_BITS(val, n, t) ((t)((val) & U32_BITMASK(n)))
#endif

/*
 * Like LO_BITS() but first right-shift `val` by `rs` bits and than get its
 * lower N-rs bits in a -Wconversion-safe way.
 *
 * NOTE: Tilck support only clang's -Wconversion, not GCC's.
 */
#define SHR_BITS(val, rs, t) LO_BITS( ((val) >> (rs)), NBITS-(rs), t )

/* Checks if 'addr' is in the range [begin, end) */
#define IN_RANGE(addr, begin, end) ((begin) <= (addr) && (addr) < (end))

/* Checks if 'addr' is in the range [begin, end] */
#define IN_RANGE_INC(addr, begin, end) ((begin) <= (addr) && (addr) <= (end))


/*
 * Brutal double-cast converting any integer to a void * pointer.
 *
 * This unsafe macro is a nice cosmetic sugar for all the cases where a integer
 * not always having pointer-size width has to be converted to a pointer.
 *
 * Typical use cases:
 *    - multiboot 1 code uses 32-bit integers for rappresenting addresses, even
 *      on 64-bit architectures.
 *
 *    - in EFI code, EFI_PHYSICAL_ADDRESS is 64-bit wide, even on 32-bit
 *      machines.
 */
#define TO_PTR(n) ((void *)(uptr)(n))

/* Includes */
#include <tilck/common/panic.h>

