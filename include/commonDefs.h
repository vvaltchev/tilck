
#pragma once

#ifdef _MSC_VER
#define inline __inline

// Make the Microsoft Intellisense happy: this does not have to work
// since we use GCC. Just, we'd like to avoid to confuse intellisense.
#define asmVolatile(...)
#define __attribute__(...)

#define asm(...)

#define ALWAYS_INLINE inline

#else

#define ALWAYS_INLINE __attribute__((always_inline))

#define asmVolatile __asm__ volatile
#define asm __asm__

#endif


typedef char int8_t;
typedef short int16_t;
typedef int int32_t;

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;

typedef long ssize_t; // signed pointer-size integer
typedef unsigned long size_t; // unsigned pointer-size integer

typedef uint32_t uintptr_t;
typedef int32_t intptr_t;
typedef int32_t ptrdiff_t;

typedef uint8_t bool;

#define true (1)
#define false (0)
#define NULL ((void *) 0)


/* This defines what the stack looks like after an ISR ran */
struct regs
{
   unsigned int gs, fs, es, ds;      /* pushed the segs last */
   unsigned int edi, esi, ebp, esp, ebx, edx, ecx, eax;  /* pushed by 'pusha' */
   unsigned int int_no, err_code;    /* our 'push byte #' and ecodes do this */
   unsigned int eip, cs, eflags, useresp, ss;   /* pushed by the processor automatically */
};

static inline void outb(uint16_t port, uint8_t val)
{
   asmVolatile("outb %0, %1" : : "a"(val), "Nd"(port));
   /* There's an outb %al, $imm8  encoding, for compile-time constant port numbers that fit in 8b.  (N constraint).
   * Wider immediate constants would be truncated at assemble-time (e.g. "i" constraint).
   * The  outb  %al, %dx  encoding is the only option for all other cases.
   * %1 expands to %dx because  port  is a uint16_t.  %w1 could be used if we had the port number a wider C type */
}

static inline uint8_t inb(uint16_t port)
{
   uint8_t ret_val;
   asmVolatile("inb %[port], %[result]"
      : [result] "=a"(ret_val)   // using symbolic operand names as an example, mainly because they're not used in order
      : [port] "Nd"(port));
   return ret_val;
}

#define halt() asmVolatile("hlt")
#define cli() asmVolatile("cli")
#define sti() asmVolatile("sti")
#define magic_debug_break() asmVolatile("xchg %bx, %bx")

#define MIN(x, y) (((x) <= (y)) ? (x) : (y))
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
