
#pragma once

#ifndef __cplusplus

typedef unsigned char bool;
#define true ((unsigned char)1)
#define false ((unsigned char)0)
#define STATIC_ASSERT(s) _Static_assert(s, "Static assertion failed")

#else

#define STATIC_ASSERT(s) static_assert(s, "Static assertion failed")

#endif

#ifdef __i386__

STATIC_ASSERT(sizeof(void *) == 4);
#define BITS32

#elif defined(__x86_64__)

STATIC_ASSERT(sizeof(void *) == 8);
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

#else

#define ALWAYS_INLINE __attribute__((always_inline)) inline

#define asmVolatile __asm__ volatile
#define asm __asm__

#define typeof(x) __typeof__(x)

#define PURE __attribute__((pure))
#define CONSTEXPR __attribute__((const))

#endif

#ifndef TEST

typedef char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;

#ifdef BITS32
typedef unsigned long long uint64_t;
typedef long long int64_t;
#else
typedef unsigned long uint64_t;
typedef long int64_t;
#endif

typedef long ssize_t; // signed pointer-size integer
typedef unsigned long size_t; // unsigned pointer-size integer

typedef size_t uintptr_t;
typedef ssize_t intptr_t;
typedef ssize_t ptrdiff_t;

#define NULL ((void *) 0)

#else

#include <cstdint>

#endif

STATIC_ASSERT(sizeof(uintptr_t) == sizeof(intptr_t));
STATIC_ASSERT(sizeof(uintptr_t) == sizeof(void *));

static ALWAYS_INLINE void outb(uint16_t port, uint8_t val)
{
   asmVolatile("outb %0, %1" : : "a"(val), "Nd"(port));
   /* There's an outb %al, $imm8  encoding, for compile-time constant port numbers that fit in 8b.  (N constraint).
   * Wider immediate constants would be truncated at assemble-time (e.g. "i" constraint).
   * The  outb  %al, %dx  encoding is the only option for all other cases.
   * %1 expands to %dx because  port  is a uint16_t.  %w1 could be used if we had the port number a wider C type */
}

static ALWAYS_INLINE uint8_t inb(uint16_t port)
{
   uint8_t ret_val;
   asmVolatile("inb %[port], %[result]"
      : [result] "=a"(ret_val)   // using symbolic operand names
      : [port] "Nd"(port));
   return ret_val;
}

#define halt() asmVolatile("hlt")
#define cli() asmVolatile("cli")
#define sti() asmVolatile("sti")
#define magic_debug_break() asmVolatile("xchg %bx, %bx")

#define MIN(x, y) (((x) <= (y)) ? (x) : (y))
#define MAX(x, y) (((x) > (y)) ? (x) : (y))

#define LIKELY(x) __builtin_expect((x), true)
#define UNLIKELY(x) __builtin_expect((x), false)

void panic(const char *fmt, ...);
void assert_failed(const char *expr, const char *file, int line);
void reboot();

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


/*
 * From: http://graphics.stanford.edu/~seander/bithacks.html
 * with custom adaptions.
 */

static inline int CONSTEXPR log2_for_power_of_2(uintptr_t v)
{
   static const uintptr_t b[] = {
      0xAAAAAAAA
      , 0xCCCCCCCC
      , 0xF0F0F0F0
      , 0xFF00FF00
      , 0xFFFF0000

#ifdef BITS64
      , 0xFFFFFFFF00000000ULL
#endif
   };

   int i;
   register uintptr_t r = (v & b[0]) != 0;


#ifdef BITS32
   for (i = 4; i > 0; i--) {
      r |= ((v & b[i]) != 0) << i;
   }
#else
   for (i = 5; i > 0; i--) {
      r |= ((v & b[i]) != 0) << i;
   }
#endif

   return r;
}

/*
* From: http://graphics.stanford.edu/~seander/bithacks.html
* with custom adaptions.
*/

CONSTEXPR static inline uintptr_t roundup_next_power_of_2(uintptr_t v)
{
   v--;
   v |= v >> 1;
   v |= v >> 2;
   v |= v >> 4;
   v |= v >> 8;
   v |= v >> 16;

#ifdef BITS64
   v |= v >> 32;
#endif

   v++;

   return v;
}

#if defined(__i386__) || defined(__x86_64__)

static ALWAYS_INLINE uint64_t RDTSC()
{
   
#ifdef BITS64
   uintptr_t lo, hi;
   asm("rdtsc" : "=a" (lo), "=d" (hi));
   return lo | (hi << 32);
#else
   uint64_t val;
   asm("rdtsc" : "=A" (val));
   return val;
#endif
}

#endif

#define DO_NOT_OPTIMIZE_AWAY(x) asmVolatile("" : "+r" ( (void *)(x) ))


static ALWAYS_INLINE void flush_tlb(uintptr_t addr)
{
   asmVolatile("invlpg (%0)" ::"r" (addr) : "memory");
}