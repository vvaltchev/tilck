
#pragma once

#include <common_defs.h>

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
               "pop %0"
               : "=g"(eflags) );

   return eflags;
}

#else

static ALWAYS_INLINE uptr get_eflags()
{
   NOT_REACHED();
   return 0;
}

#endif



#if !defined(TESTING) && !defined(KERNEL_TEST)
#  define HW_disable_interrupts() asmVolatile("cli")
#  define HW_enable_interrupts() asmVolatile("sti")
#else
#  define HW_disable_interrupts()
#  define HW_enable_interrupts()
#endif


extern volatile bool in_panic;
extern volatile int disable_interrupts_count;


static inline bool are_interrupts_enabled_int(const char *file, int line)
{
   uptr eflags = get_eflags();
   bool interrupts_on = !!(eflags & EFLAGS_IF);

#ifdef DEBUG

   if (interrupts_on) {
      // If the interrupts are ON, we have to disable them just in order to
      // check the value of disable_interrupts_count.
      HW_disable_interrupts();
   }

   if (interrupts_on != (disable_interrupts_count == 0)) {
      if (!in_panic) {
         panic("FAILED interrupts check.\nFile: %s on line %i.\n"
               "interrupts_on: %s\ndisable_interrupts_count: %i",
               file, line, interrupts_on ? "TRUE" : "FALSE",
               disable_interrupts_count);
      }
   }

   if (interrupts_on) {
      HW_enable_interrupts();
   }

#endif

   return interrupts_on;
}

#define are_interrupts_enabled() are_interrupts_enabled_int(__FILE__, __LINE__)

static ALWAYS_INLINE void enable_interrupts_forced()
{
   disable_interrupts_count = 0;
   HW_enable_interrupts();
}

static ALWAYS_INLINE void disable_interrupts_forced()
{
   HW_disable_interrupts();
   disable_interrupts_count = 1;
}

#ifndef UNIT_TEST_ENVIRONMENT

static ALWAYS_INLINE void enable_interrupts()
{
   ASSERT(!are_interrupts_enabled());
   ASSERT(disable_interrupts_count > 0);

   if (--disable_interrupts_count == 0) {
      HW_enable_interrupts();
   }
}

static ALWAYS_INLINE void disable_interrupts()
{
   uptr eflags = get_eflags();

   if (eflags & EFLAGS_IF) {

      // interrupts are enabled: disable them first.
      HW_disable_interrupts();

      ASSERT(disable_interrupts_count == 0);

   } else {

      // interrupts are already disabled: just increase the counter.
      ASSERT(disable_interrupts_count > 0);
   }

   ++disable_interrupts_count;
}

#else

#define enable_interrupts()
#define disable_interrupts()

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
