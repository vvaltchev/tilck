/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#define _TILCK_BASIC_DEFS_H_

#include <tilck_gen_headers/config_global.h>

/*
 * TESTING is defined when kernel unit tests are compiled and it affects
 * actual kernel headers but NOT kernel's C files.
 *
 * KERNEL_TEST is defined when the noarch part of the kernel is compiled for
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

#if (defined(__TILCK_KERNEL__)                         ||  \
    defined(UNIT_TEST_ENVIRONMENT)                     ||  \
    defined(__cplusplus)) && !defined(__MOD_ACPICA__)

   #include <tilck_gen_headers/config_kernel.h>
#endif

#ifdef __cplusplus

   #if !KERNEL_FORCE_TC_ISYSTEM

      /* Default case: real kernel build */
      #include <cstdint>     // system header
      #include <climits>     // system header

   #else

      /* Special case: non-runnable static analysis build */
      #include <stdint.h>    // system header
      #include <limits.h>    // system header

   #endif

   #define STATIC_ASSERT(s) static_assert(s, "Static assertion failed")

#else

   #include <stdint.h>    // system header
   #include <stddef.h>    // system header
   #include <stdbool.h>   // system header
   #include <stdalign.h>  // system header
   #include <inttypes.h>  // system header
   #include <limits.h>    // system header
   #define STATIC_ASSERT(s) _Static_assert(s, "Static assertion failed")

#endif // #ifdef __cplusplus

#ifndef __USE_MISC
   #define __USE_MISC
#endif


#include <stdarg.h>
#include <sys/types.h>    // system header (just for ulong)

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

   #ifndef __cplusplus
      #define NORETURN _Noreturn /* C11 no return attribute */
   #else
      #undef NULL
      #define NULL nullptr
      #define NORETURN [[ noreturn ]] /* C++11 no return attribute */
   #endif

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
#define ATTR_PRINTF_LIKE(c) __attribute__ ((__format__ (__printf__, c, c+1)))
#define ATTR_SECTION(s) __attribute__ ((section (s)))

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

/* Pointer-size signed integer */
/*
 * Just use `long`. We support only LP64 compilers and 32/64 bit architectures.
 * 16-bit architectures where sizeof(long) > sizeof(void *) won't be supported.
 */

/* Pointer-size unsigned integer */
/* Just use `ulong` (from sys/types.h). Reason: see the comment for long */

/* What we're relying on */
STATIC_ASSERT(sizeof(ulong) == sizeof(long));
STATIC_ASSERT(sizeof(ulong) == sizeof(void *));

/*
 * Tilck's off_t, unrelated with any external files.
 *
 * The size of `offt` determine the max size of files and devices that Tilck
 * can access. By default, it's a signed 64-bit integer and that doesn't have
 * a measurable impact on Tilck's performance even on 32-bit systems where
 * 64-bit integers require multiple instructions and two registers. But, the
 * size of `offt` is configurable in order to force the code to not make direct
 * assumptions about its size. Also, on some small-scale embedded systems, it
 * might be convenient to consider disabling the KERNEL_64BIT_OFFT and save
 * some space in structs, along with a few CPU cycles.
 */

#ifdef HAVE_KERNEL_CONFIG

   #if KERNEL_64BIT_OFFT
      typedef int64_t offt;
      #define OFFT_MAX        ((offt)INT64_MAX)
   #else
      typedef long offt;
      #define OFFT_MAX        ((offt)LONG_MAX)
   #endif

   STATIC_ASSERT(sizeof(offt) >= sizeof(long));
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
 * UNSAFE against double-evaluation MIN, MAX, ABS, CLAMP macros.
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

#define UNSAFE_ABS(x) ((x) >= 0 ? (x) : -(x))

#define UNSAFE_CLAMP(val, minval, maxval)                             \
   UNSAFE_MIN(UNSAFE_MAX((val), (minval)), (maxval))

/*
 * SAFE against double-evaluation MIN, MAX, ABS, CLAMP macros.
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

#define CLAMP(val, minval, maxval)                                    \
   ({                                                                 \
      const typeof(val) CONCAT(_v, __LINE__) = (val);                 \
      const typeof(minval) CONCAT(_mv, __LINE__) = (minval);          \
      const typeof(maxval) CONCAT(_Mv, __LINE__) = (maxval);          \
      UNSAFE_CLAMP(CONCAT(_v, __LINE__),                              \
                   CONCAT(_mv, __LINE__),                             \
                   CONCAT(_Mv, __LINE__));                            \
   })

#define ABS(x)                                                        \
   ({                                                                 \
      const typeof(x) CONCAT(_v, __LINE__) = (x);                     \
      UNSAFE_ABS(CONCAT(_v, __LINE__));                               \
   })

#define LIKELY(x) __builtin_expect((x), true)
#define UNLIKELY(x) __builtin_expect((x), false)

#define ARRAY_SIZE(a) ((int)(sizeof(a)/sizeof((a)[0])))
#define CONTAINER_OF(elem_ptr, struct_type, mem_name)                 \
   (                                                                  \
      (struct_type *)(void *)(                                        \
         ((char *)elem_ptr) - OFFSET_OF(struct_type, mem_name)        \
      )                                                               \
   )

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
#define TO_PTR(n) ((void *)(ulong)(n))

#ifndef __clang__

   #define DO_NOT_OPTIMIZE_AWAY(x) asmVolatile("" : "+r" ( TO_PTR(x) ))

#else

   static ALWAYS_INLINE void __do_not_opt_away(void *x)
   {
      asmVolatile("" ::: "memory");
   }

   #define DO_NOT_OPTIMIZE_AWAY(x) (__do_not_opt_away((void *)(x)))

#endif

#define ALIGNED_MASK(n)                               (~((n) - 1))
#define POINTER_ALIGN_MASK            ALIGNED_MASK(sizeof(void *))

// Standard compare function signature among generic objects.
typedef long (*cmpfun_ptr)(const void *a, const void *b);

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
 * NOTE: Tilck support only Clang's -Wconversion, not GCC's.
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
 * With modern GCC compilers, things like *(u32 *)(ptr + off) are undefined
 * behavior if `ptr+off` is not aligned correctly, even on architectures
 * like x86 that natively support unaligned access. That's because in the C
 * standard (since C89) unaligned access is considered UB.
 *
 * From C11 (draft N1570):
 * <<
 *    Conversion between two pointer types produces a result that is
 *    incorrectly aligned (6.3.2.3).
 * >>
 *
 * See GCC's bug: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=93031
 *
 * The correct solution to read/write values where the address might be
 * unaligned is to use __builtin_memcpy(): it will just emit a single MOV
 * instruction on x86, while it could do more on architectures where unaligned
 * access is not allowed.
 */

#define SAFE_READ(addr, type) ({                                \
   type __rres;                                                 \
   __builtin_memcpy(&__rres, (void *)(addr), sizeof(__rres));   \
   __rres;                                                      \
})

#define SAFE_WRITE(addr, type, val) ({                          \
   const type __wtmp = (val);                                   \
   __builtin_memcpy((void *)(addr), &__wtmp, sizeof(__wtmp));   \
})

#define READ_U16(addr)          SAFE_READ((addr), u16)
#define READ_S16(addr)          SAFE_READ((addr), s16)
#define READ_U32(addr)          SAFE_READ((addr), u32)
#define READ_S32(addr)          SAFE_READ((addr), s32)
#define READ_U64(addr)          SAFE_READ((addr), u64)
#define READ_S64(addr)          SAFE_READ((addr), s64)
#define READ_ULONG(addr)        SAFE_READ((addr), ulong)
#define READ_LONG(addr)         SAFE_READ((addr), long)
#define READ_PTR(addr)          SAFE_READ((addr), void *)

#define WRITE_U16(addr, val)    SAFE_WRITE((addr), u16, (val))
#define WRITE_S16(addr, val)    SAFE_WRITE((addr), s16, (val))
#define WRITE_U32(addr, val)    SAFE_WRITE((addr), u32, (val))
#define WRITE_S32(addr, val)    SAFE_WRITE((addr), s32, (val))
#define WRITE_U64(addr, val)    SAFE_WRITE((addr), u64, (val))
#define WRITE_S64(addr, val)    SAFE_WRITE((addr), s64, (val))
#define WRITE_ULONG(addr, val)  SAFE_WRITE((addr), ulong, (val))
#define WRITE_LONG(addr, val)   SAFE_WRITE((addr), long, (val))
#define WRITE_PTR(addr, val)    SAFE_WRITE((addr), void *, (val))


/* Other utils */
enum tristate {
   tri_unknown = -1,
   tri_no      = 0,
   tri_yes     = 1,
};

/* Includes */
#include <tilck/common/panic.h>

