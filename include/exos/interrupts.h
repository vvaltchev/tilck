
#pragma once
#include <exos/hal.h>

extern volatile int nested_interrupts_count;
extern volatile int nested_interrupts[32];

void set_fault_handler(int fault, void *ptr);

void check_not_in_irq_handler(void);
bool in_syscall(void);
int get_curr_interrupt(void);

// NOTE: this function is x86-dependent
static ALWAYS_INLINE bool is_irq(int int_num)
{
   return int_num >= 32 && int_num != SYSCALL_SOFT_INTERRUPT;
}

// NOTE: this function is x86-dependent
static ALWAYS_INLINE bool is_fault(int int_num)
{
   return 0 <= int_num && int_num < 32;
}

static inline void push_nested_interrupt(int int_num)
{
   uptr var;
   disable_interrupts(&var);
   {
      ASSERT(nested_interrupts_count < (int)ARRAY_SIZE(nested_interrupts));
      ASSERT(nested_interrupts_count >= 0);
      nested_interrupts[nested_interrupts_count++] = int_num;
   }
   enable_interrupts(&var);
}

static inline void pop_nested_interrupt(void)
{
   uptr var;
   disable_interrupts(&var);
   {
      nested_interrupts_count--;
      ASSERT(nested_interrupts_count >= 0);
   }
   enable_interrupts(&var);
}
