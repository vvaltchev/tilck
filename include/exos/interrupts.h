
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

static inline bool are_interrupts_enabled_int(const char *file, int line)
{
   return !!(get_eflags() & EFLAGS_IF);
}

#define are_interrupts_enabled() are_interrupts_enabled_int(__FILE__, __LINE__)


#ifndef UNIT_TEST_ENVIRONMENT

static inline void enable_interrupts(uptr *stack_var)
{
   ASSERT(!are_interrupts_enabled());
   ASSERT(disable_interrupts_count > 0);

   if (--disable_interrupts_count == 0) {
      HW_enable_interrupts();
   }
}

static inline void disable_interrupts(uptr *stack_var)
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

static inline void enable_interrupts(uptr *stack_var) { }
static inline void disable_interrupts(uptr *stack_var) { }

#endif // ifndef UNIT_TEST_ENVIRONMENT

static inline void push_nested_interrupt(int int_num)
{
   ASSERT(nested_interrupts_count < (int)ARRAY_SIZE(nested_interrupts));
   ASSERT(nested_interrupts_count >= 0);
   nested_interrupts[nested_interrupts_count++] = int_num;
}

static inline void pop_nested_interrupt(void)
{
   nested_interrupts_count--;
   ASSERT(nested_interrupts_count >= 0);
}
