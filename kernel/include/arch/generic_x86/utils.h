
#pragma once

#include <common_defs.h>

#if !defined(__i386__) && !defined(__x86_64__)
#error This header can be used only for x86 and x86-64 architectures.
#endif

#define TIMER_HZ 100

static ALWAYS_INLINE u64 RDTSC()
{
#ifdef BITS64
   uptr lo, hi;
   asm("rdtsc" : "=a" (lo), "=d" (hi));
   return lo | (hi << 32);
#else
   u64 val;
   asm("rdtsc" : "=A" (val));
   return val;
#endif
}

static ALWAYS_INLINE void outb(u16 port, u8 val)
{
   /*
    * There's an outb %al, $imm8  encoding, for compile-time constant port
    * numbers that fit in 8b.  (N constraint). Wider immediate constants
    * would be truncated at assemble-time (e.g. "i" constraint).
    * The  outb  %al, %dx  encoding is the only option for all other cases.
    * %1 expands to %dx because  port  is a u16.  %w1 could be used if we had
    * the port number a wider C type.
    */
   asmVolatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static ALWAYS_INLINE u8 inb(u16 port)
{
   u8 ret_val;
   asmVolatile("inb %[port], %[result]"
      : [result] "=a"(ret_val)   // using symbolic operand names
      : [port] "Nd"(port));
   return ret_val;
}

#define halt() asmVolatile("hlt")
#define cli() asmVolatile("cli")
#define sti() asmVolatile("sti")
