
#pragma once

#include <common_defs.h>
#include <hal.h>


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

// Forward-declaring regs
typedef struct regs regs;

void irq_install();

void irq_install_handler(u8 irq, interrupt_handler h);
void irq_uninstall_handler(u8 irq);

void irq_set_mask(u8 IRQline);
void irq_clear_mask(u8 IRQline);

void set_fault_handler(int fault, void *ptr);
void PIC_sendEOI(u8 irq);

u16 pic_get_irr(void);
u16 pic_get_isr(void);
u32 pic_get_imr(void);

extern volatile int nested_interrupts_count;
extern volatile int nested_interrupts[32];

void pop_nested_interrupt();
void push_nested_interrupt(int int_num);


static inline int get_curr_interrupt()
{
   ASSERT(!are_interrupts_enabled());
   return nested_interrupts[nested_interrupts_count - 1];
}


static ALWAYS_INLINE bool is_irq(int interrupt_num)
{
   return interrupt_num != -1 &&
          interrupt_num >= 32 &&
          interrupt_num != SYSCALL_SOFT_INTERRUPT;
}

void check_not_in_irq_handler(void);

// TODO: re-implement this when SYSENTER is supported as well.
static inline bool in_syscall()
{
   bool res = false;
   disable_interrupts();

   for (int i = nested_interrupts_count - 1; i >= 0; i--) {
      if (nested_interrupts[i] == 0x80) {
         res = true;
         break;
      }
   }

   enable_interrupts();
   return res;
}

/*
 * -----------------------------------------------------------------------------
 *
 * DEBUG STUFF
 *
 * -----------------------------------------------------------------------------
 */


extern volatile int disable_interrupts_count;
extern volatile u64 jiffies;

static inline void DEBUG_check_disable_interrupts_count_is_0(int int_num)
{
#ifdef DEBUG

   int disable_int_c = disable_interrupts_count;

   if (disable_int_c != 0) {

      /*
       * Disable the interrupts in the lowest-level possible in case,
       * for any reason, they are actually enabled.
       */
      HW_disable_interrupts();

      panic("[generic_interrupt_handler] int_num: %i\n"
            "disable_interrupts_count: %i (expected: 0)\n"
            "total system ticks: %llu\n",
            int_num, disable_int_c, jiffies);
   }

#endif
}
