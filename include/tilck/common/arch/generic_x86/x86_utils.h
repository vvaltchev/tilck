/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#if !defined(__i386__) && !defined(__x86_64__)
   #error This header can be used only for x86 and x86-64 architectures.
#endif

#include <tilck/common/basic_defs.h>
#include <tilck/common/arch/generic_x86/asm_consts.h>

/*
 * HACK: include directly <ia32intrin.h> because <x86intrin.h> includes too much
 * stuff like FPU-related funcs which don't compile on x86_64 because we have
 * disabled FPU instructions.
 */
#define _X86INTRIN_H_INCLUDED    /* for GCC */
#define _X86GPRINTRIN_H_INCLUDED /* for GCC 11 */
#define __X86INTRIN_H            /* for clang */
#include <ia32intrin.h>

#define X86_PC_TIMER_IRQ           0
#define X86_PC_KEYBOARD_IRQ        1
#define X86_PC_COM2_COM4_IRQ       3
#define X86_PC_COM1_COM3_IRQ       4
#define X86_PC_SOUND_IRQ           5
#define X86_PC_FLOPPY_IRQ          6
#define X86_PC_LPT1_OR_SLAVE_IRQ   7
#define X86_PC_RTC_IRQ             8
#define X86_PC_ACPI_IRQ            9
#define X86_PC_PCI1_IRQ           10
#define X86_PC_PCI2_IRQ           11
#define X86_PC_PS2_MOUSE_IRQ      12
#define X86_PC_MATH_COPROC_IRQ    13
#define X86_PC_HD_IRQ             14


/*
 * The following FAULTs are valid both for x86 (i386+) and for x86_64.
 */
#define FAULT_DIVISION_BY_ZERO      0
#define FAULT_DEBUG                 1
#define FAULT_NMI                   2
#define FAULT_BREAKPOINT            3
#define FAULT_INTO_DEC_OVERFLOW     4
#define FAULT_OUT_OF_BOUNDS         5
#define FAULT_INVALID_OPCODE        6
#define FAULT_NO_COPROC             7

#define FAULT_DOUBLE_FAULT          8
#define FAULT_COPROC_SEG_OVERRRUN   9
#define FAULT_BAD_TSS              10
#define FAULT_SEG_NOT_PRESENT      11
#define FAULT_STACK_FAULT          12
#define FAULT_GENERAL_PROTECTION   13
#define FAULT_PAGE_FAULT           14
#define FAULT_UNKNOWN_INTERRUPT    15
#define FAULT_COPROC_FAULT         16
#define FAULT_ALIGN_FAULT          17
#define FAULT_MACHINE_CHECK        18

#define SYSCALL_SOFT_INTERRUPT   0x80

#define MEM_TYPE_UC         0x00 // Uncacheable
#define MEM_TYPE_WC         0x01 // Write Combining
#define MEM_TYPE_R1         0x02 // Reserved 1
#define MEM_TYPE_R2         0x03 // Reserved 2
#define MEM_TYPE_WT         0x04 // Write Through
#define MEM_TYPE_WP         0x05 // Write Protected
#define MEM_TYPE_WB         0x06 // Write Back
#define MEM_TYPE_UC_        0x07 // Uncached (PAT only)

#define COM1 0x3f8
#define COM2 0x2f8
#define COM3 0x3e8
#define COM4 0x2e8

/*
 * x86 selectors are 16 bit integers:
 *
 * +------------------+----------------+--------------+
 * | index [13 bits]  | table [1 bit]  | RPL [2 bits] |
 * +------- ----------+----------------+--------------+
 *
 * table:
 *    0 means the index is in GDT
 *    1 means the index is in LDT
 *
 * RPL: requested privilege level [0..3]
 */

#define TABLE_GDT 0
#define TABLE_LDT 1
#define X86_SELECTOR(idx, table, rpl) ((u16)((idx << 3) | (table << 2) | (rpl)))

/* Defines useful when calling fault_resumable_call() */
#define ALL_FAULTS_MASK (0xFFFFFFFF)
#define PAGE_FAULT_MASK (1 << FAULT_PAGE_FAULT)

#define RDTSC() __rdtsc()


#define MEM_CLOBBER(var, type, size)  (*(type (*)[size])(var))

#if defined(__GNUC__) && !defined(__clang__)

   #define MEM_CLOBBER_NOSZ(var, type)   (*(type (*)[])(var))

#else

   /*
    * NOTE: clang does not support the *(type (*)[])(var) syntax to clobber
    * memory areas with unspecified size.
    *
    * See: https://bugs.llvm.org/show_bug.cgi?id=47866
    */

   #define MEM_CLOBBER_NOSZ(var, type)   MEM_CLOBBER((var), type, 64 * KB)

#endif

static ALWAYS_INLINE void outb(u16 port, u8 val)
{
   asmVolatile(
      "outb %[value], %[port]"
      : /* no output */
      : [value] "a"(val), [port] "Nd"(port)
   );
}

static ALWAYS_INLINE u8 inb(u16 port)
{
   u8 ret;
   asmVolatile(
      "inb %[port], %[result]"
      : [result] "=a"(ret)
      : [port] "Nd"(port)
   );
   return ret;
}

static ALWAYS_INLINE void outw(u16 port, u16 val)
{
   asmVolatile(
      "outw %[value], %[port]"
      : /* no output */
      : [value] "a"(val), [port] "Nd"(port)
   );
}

static ALWAYS_INLINE u16 inw(u16 port)
{
   u16 ret;
   asmVolatile(
      "inw %[port], %[result]"
      : [result] "=a"(ret)
      : [port] "Nd"(port)
   );
   return ret;
}

static ALWAYS_INLINE void outl(u16 port, u32 val)
{
   asmVolatile(
      "outl %[value], %[port]"
      : /* no output */
      : [value] "a"(val), [port] "Nd"(port)
   );
}

static ALWAYS_INLINE u32 inl(u16 port)
{
   u32 ret;
   asmVolatile(
      "inl %[port], %[result]"
      : [result] "=a"(ret)
      : [port] "Nd"(port)
   );
   return ret;
}

static ALWAYS_INLINE void halt(void)
{
   asmVolatile("hlt");
}

static ALWAYS_INLINE void wrmsr(u32 msr_id, u64 msr_value)
{
   asmVolatile( "wrmsr" : : "c" (msr_id), "A" (msr_value) );
}

static ALWAYS_INLINE u64 rdmsr(u32 msr_id)
{
   u64 msr_value;
   asmVolatile( "rdmsr" : "=A" (msr_value) : "c" (msr_id) );
   return msr_value;
}

static ALWAYS_INLINE ulong get_eflags(void)
{
   ulong eflags;
   asmVolatile("pushf\n\t"
               "pop %0\n\t"
               : "=g"(eflags) );

   return eflags;
}

static ALWAYS_INLINE void set_eflags(ulong f)
{
   asmVolatile("push %0\n\t"
               "popf\n\t"
               : /* no output */
               : "r" (f)
               : "cc");
}

static ALWAYS_INLINE void enable_interrupts_forced(void)
{
#ifndef UNIT_TEST_ENVIRONMENT
   asmVolatile("sti");
#endif
}

static ALWAYS_INLINE void disable_interrupts_forced(void)
{
#ifndef UNIT_TEST_ENVIRONMENT
   asmVolatile("cli");
#endif
}

static ALWAYS_INLINE bool are_interrupts_enabled(void)
{
   return !!(get_eflags() & EFLAGS_IF);
}

static ALWAYS_INLINE void disable_interrupts(ulong *const var)
{
   *var = get_eflags();

   if (*var & EFLAGS_IF) {
      disable_interrupts_forced();
   }
}

static ALWAYS_INLINE void enable_interrupts(const ulong *const var)
{
   if (*var & EFLAGS_IF) {
      enable_interrupts_forced();
   }
}

/*
 * Invalidates the TLB entry used for resolving the page containing 'vaddr'.
 */
static ALWAYS_INLINE void invalidate_page_hw(ulong vaddr)
{
   asmVolatile("invlpg (%0)"
               : /* no output */
               :"r" (vaddr)
               : "memory");
}

static ALWAYS_INLINE void write_back_and_invl_cache(void)
{
   asmVolatile("wbinvd");
}

static ALWAYS_INLINE void cpuid(u32 code, u32 *a, u32 *b, u32 *c, u32 *d)
{
    asm("cpuid"
        : "=a"(*a), "=b" (*b), "=c" (*c), "=d"(*d)
        : "a"(code), "b" (0), "c" (0), "d" (0)
        : "memory");
}

static ALWAYS_INLINE ulong read_cr0(void)
{
   ulong res;
   asmVolatile("mov %%cr0, %0"
               : "=r" (res)
               : /* no input */
               : /* no clobber */);

   return res;
}

static ALWAYS_INLINE void write_cr0(ulong val)
{
   asmVolatile("mov %0, %%cr0"
               : /* no output */
               : "r" (val)
               : /* no clobber */);
}

static ALWAYS_INLINE ulong read_cr3(void)
{
   ulong res;
   asmVolatile("mov %%cr3, %0"
               : "=r" (res)
               : /* no input */
               : /* no clobber */);

   return res;
}

static ALWAYS_INLINE void write_cr3(ulong val)
{
   asmVolatile("mov %0, %%cr3"
               : /* no output */
               : "r" (val)
               : /* no clobber */);
}

static ALWAYS_INLINE ulong read_cr4(void)
{
   ulong res;
   asmVolatile("mov %%cr4, %0"
               : "=r" (res)
               : /* no input */
               : /* no clobber */);

   return res;
}

static ALWAYS_INLINE void write_cr4(ulong val)
{
   asmVolatile("mov %0, %%cr4"
               : /* no output */
               : "r" (val)
               : /* no clobber */);
}

static ALWAYS_INLINE void hw_fpu_enable(void)
{
   write_cr0(read_cr0() & ~CR0_TS);
}

static ALWAYS_INLINE void hw_fpu_disable(void)
{
   write_cr0(read_cr0() | CR0_TS);
}

static ALWAYS_INLINE bool hw_is_fpu_enabled(void)
{
   return !(read_cr0() & CR0_TS);
}

static ALWAYS_INLINE ulong __get_curr_pdir()
{
   return read_cr3();
}

static ALWAYS_INLINE void __set_curr_pdir(ulong paddr)
{
   write_cr3(paddr);
}

void i8042_reboot(void);
