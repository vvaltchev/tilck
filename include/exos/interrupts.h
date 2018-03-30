
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


#ifndef UNIT_TEST_ENVIRONMENT

static inline void enable_interrupts(void)
{
   ASSERT(!are_interrupts_enabled());
   ASSERT(disable_interrupts_count > 0);

   if (--disable_interrupts_count == 0) {
      HW_enable_interrupts();
   }
}

static inline void disable_interrupts(void)
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

static inline void enable_interrupts(void) { }
static inline void disable_interrupts(void) { }

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
