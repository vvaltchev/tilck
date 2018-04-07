
#pragma once

#include <common/basic_defs.h>

#if !defined(__i386__) && !defined(__x86_64__)
#error This header can be used only for x86 and x86-64 architectures.
#endif

#define TIMER_HZ 250

#define X86_PC_TIMER_IRQ       0
#define X86_PC_KEYBOARD_IRQ    1
#define X86_PC_RTC_IRQ         8
#define X86_PC_ACPI_IRQ        9
#define X86_PC_PS2_MOUSE_IRQ  12

#define EFLAGS_CF    0x0001
#define EFLAGS_PF    0x0004
#define EFLAGS_AF    0x0010
#define EFLAGS_ZF    0x0040
#define EFLAGS_SF    0x0080
#define EFLAGS_TF    0x0100
#define EFLAGS_IF    0x0200
#define EFLAGS_DF    0x0400
#define EFLAGS_OF    0x0800
#define EFLAGS_NT    0x4000
#define EFLAGS_RF   0x10000
#define EFLAGS_VM   0x20000
#define EFLAGS_AC   0x40000
#define EFLAGS_VIF  0x80000
#define EFLAGS_VIP 0x100000
#define EFLAGS_ID  0x200000

#define EFLAGS_IOPL 0x3000

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

extern volatile bool in_panic;

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
#define X86_SELECTOR(idx, table, rpl) ((idx << 3) | (table << 2) | (rpl))

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

static ALWAYS_INLINE void halt()
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


#if defined(BITS32) && !defined(UNIT_TEST_ENVIRONMENT)

static ALWAYS_INLINE uptr get_eflags()
{
   uptr eflags;
   asmVolatile("pushf\n\t"
               "pop %0\n\t"
               : "=g"(eflags) );

   return eflags;
}

static ALWAYS_INLINE void set_eflags(uptr f)
{
   asmVolatile("push %0\n\t"
               "popf\n\t"
               : /* no output */
               : "r" (f)
               : "cc");
}

#else

static ALWAYS_INLINE uptr get_eflags() { NOT_REACHED(); }
static ALWAYS_INLINE void set_eflags(uptr f) { NOT_REACHED(); }

#endif



#ifndef UNIT_TEST_ENVIRONMENT

static ALWAYS_INLINE void enable_interrupts_forced(void)
{
   asmVolatile("sti");
}

static ALWAYS_INLINE void disable_interrupts_forced(void)
{
   asmVolatile("cli");
}

static ALWAYS_INLINE bool are_interrupts_enabled(void)
{
   return !!(get_eflags() & EFLAGS_IF);
}

static ALWAYS_INLINE void disable_interrupts(uptr *const var)
{
   *var = get_eflags();

   if (*var & EFLAGS_IF) {
      disable_interrupts_forced();
   }
}

static ALWAYS_INLINE void enable_interrupts(const uptr *const var)
{
   if (*var & EFLAGS_IF) {
      enable_interrupts_forced();
   }
}

#else

static ALWAYS_INLINE void enable_interrupts_forced(void) { }
static ALWAYS_INLINE void disable_interrupts_forced(void) { }
static ALWAYS_INLINE void disable_interrupts(uptr *stack_var) { }
static ALWAYS_INLINE void enable_interrupts(uptr *stack_var) { }
static ALWAYS_INLINE bool are_interrupts_enabled(void) { NOT_REACHED(); }

#endif // ifndef UNIT_TEST_ENVIRONMENT

static ALWAYS_INLINE void cpuid(int code, u32 *a, u32 *d)
{
    asmVolatile( "cpuid" : "=a"(*a), "=d"(*d) : "0"(code) : "ebx", "ecx" );
}

/*
 * Invalidates the TLB entry used for resolving the page containing 'vaddr'.
 */
static ALWAYS_INLINE void invalidate_page(uptr vaddr)
{
   asmVolatile("invlpg (%0)" ::"r" (vaddr) : "memory");
}


void validate_stack_pointer_int(const char *file, int line);

#ifdef DEBUG
#  define DEBUG_VALIDATE_STACK_PTR() validate_stack_pointer_int(__FILE__, \
                                                                __LINE__)
#else
#  define DEBUG_VALIDATE_STACK_PTR()
#endif

// Turn off the machine using a debug qemu-only mechnism
void debug_qemu_turn_off_machine();

// Reboot the system
void reboot();
