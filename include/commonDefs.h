
#pragma once

#ifdef _MSC_VER
#define inline __inline

// Make the Microsoft Intellisense happy: this does not have to work
// since we use GCC. Just, we'd like to avoid to confuse intellisense.
#define asmVolatile(...)
#define __attribute__(...)

#define asm(...)

#define ALWAYS_INLINE inline
#define typeof(x) void *

#define static_assert(s,err)

#define PURE
#define CONSTEXPR

#else

#define ALWAYS_INLINE __attribute__((always_inline)) inline

#define asmVolatile __asm__ volatile
#define asm __asm__

#define typeof(x) __typeof__(x)

#define static_assert(s) _Static_assert(s, "Static assertion failed")

#define PURE __attribute__((pure))
#define CONSTEXPR __attribute__((const))

#endif

typedef char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;

typedef long ssize_t; // signed pointer-size integer
typedef unsigned long size_t; // unsigned pointer-size integer

typedef size_t uintptr_t;
typedef ssize_t intptr_t;
typedef ssize_t ptrdiff_t;

static_assert(sizeof(uintptr_t) == sizeof(intptr_t));
static_assert(sizeof(uintptr_t) == sizeof(void *));

#ifdef __i386__

static_assert(sizeof(void *) == 4);

#elif defined(__x86_64__)

static_assert(sizeof(void *) == 8);

#else

#error Platform not supported.

#endif


typedef uint8_t bool;
#define true ((uint8_t)1)
#define false ((uint8_t)0)
#define NULL ((void *) 0)


/* This defines what the stack looks like after an ISR ran */
typedef struct {
   uint32_t gs, fs, es, ds;      /* pushed the segs last */
   uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;  /* pushed by 'pusha' */
   uint32_t int_no, err_code;    /* our 'push byte #' and ecodes do this */
   uint32_t eip, cs, eflags, useresp, ss;   /* pushed by the processor automatically */
} regs;

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

#define ASSERT(x)                                                    \
   do {                                                              \
      if (UNLIKELY(!(x))) {                                          \
         assert_failed(#x , __FILE__, __LINE__);                     \
      }                                                              \
   } while (0)

static inline int CONSTEXPR log2(size_t n)
{
   int res = 0;

   while (n > 1) {
      res++;
      n >>= 1;
   }

   return res;
}