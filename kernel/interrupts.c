/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/interrupts.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/hal.h>

void handle_syscall(regs_t *);
void handle_fault(regs_t *);
void handle_irq(regs_t *r);

#if KRN_TRACK_NESTED_INTERR

static int nested_interrupts_count;
static int nested_interrupts[MAX_NESTED_INTERRUPTS] =
{
   [0 ... MAX_NESTED_INTERRUPTS-1] = -1,
};

inline void push_nested_interrupt(int int_num)
{
   ulong var;
   disable_interrupts(&var); /* under #if KRN_TRACK_NESTED_INTERR */
   {
      ASSERT(nested_interrupts_count < MAX_NESTED_INTERRUPTS);
      ASSERT(nested_interrupts_count >= 0);
      nested_interrupts[nested_interrupts_count++] = int_num;
   }
   enable_interrupts(&var);
}

inline void pop_nested_interrupt(void)
{
   ulong var;
   disable_interrupts(&var); /* under #if KRN_TRACK_NESTED_INTERR */
   {
      nested_interrupts_count--;
      ASSERT(nested_interrupts_count >= 0);
   }
   enable_interrupts(&var);
}

bool in_nested_irq_num(int irq_num)
{
   ASSERT(!are_interrupts_enabled());

   for (int i = nested_interrupts_count - 2; i >= 0; i--)
      if (int_to_irq(nested_interrupts[i]) == irq_num)
         return true;

   return false;
}

void check_not_in_irq_handler(void)
{
   ulong var;

   if (!in_panic()) {
      disable_interrupts(&var); /* under #if KRN_TRACK_NESTED_INTERR */
      {
         if (nested_interrupts_count > 0)
            if (is_irq(nested_interrupts[nested_interrupts_count - 1]))
               panic("NOT expected to be in an IRQ handler");
      }
      enable_interrupts(&var);
   }
}

void check_in_no_other_irq_than_timer(void)
{
   ulong var;

   if (in_panic())
      return;

   disable_interrupts(&var); /* under #if KRN_TRACK_NESTED_INTERR */
   {
      if (nested_interrupts_count > 0) {

         int n = nested_interrupts[nested_interrupts_count - 1];

         if (is_irq(n) && !is_timer_irq(n))
            panic("NOT expected to be in an IRQ handler != IRQ0 [timer]");
      }
   }
   enable_interrupts(&var);
}

void check_in_irq_handler(void)
{
   ulong var;

   if (!in_panic()) {

      disable_interrupts(&var); /* under #if KRN_TRACK_NESTED_INTERR */

      if (nested_interrupts_count > 0) {
         if (is_irq(nested_interrupts[nested_interrupts_count - 1])) {
            enable_interrupts(&var);
            return;
         }
      }

      panic("Expected TO BE in an IRQ handler (but we're NOT)");
   }
}

bool in_syscall(void)
{
   ASSERT(!are_interrupts_enabled());
   bool res = false;

   for (int i = nested_interrupts_count - 1; i >= 0; i--) {
      if (nested_interrupts[i] == SYSCALL_SOFT_INTERRUPT) {
         res = true;
         break;
      }
   }

   return res;
}

extern u32 slow_timer_irq_handler_count;

static void DEBUG_check_not_same_interrupt_nested(int int_num)
{
   ASSERT(!are_interrupts_enabled());

   for (int i = nested_interrupts_count - 1; i >= 0; i--)
      if (nested_interrupts[i] == int_num) {

         if (int_num == 32) {
            /* tollarate nested IRQ 0 for debug purposes */
            return;
         }

         panic("Same interrupt (%i) twice in nested_interrupts[]", int_num);
      }
}


void nested_interrupts_drop_top_syscall(void)
{
   if (nested_interrupts_count > 0) {
      ASSERT(nested_interrupts_count == 1);
      ASSERT(nested_interrupts[0] == SYSCALL_SOFT_INTERRUPT);
      pop_nested_interrupt();
   }
}

void panic_dump_nested_interrupts(void)
{
   VERIFY(in_panic());
   ASSERT(!are_interrupts_enabled());

   char buf[128];
   int written = 0;

   written += snprintk(buf + written, sizeof(buf), "Interrupts: [ ");

   for (int i = nested_interrupts_count - 1; i >= 0; i--) {
      written += snprintk(buf + written, sizeof(buf) - (u32)written,
                          "%i ", nested_interrupts[i]);
   }

   /* written += */ snprintk(buf + written, sizeof(buf) - (u32) written, "]\n");
   printk("%s", buf);
}

int get_nested_interrupts_count(void)
{
   ASSERT(!are_interrupts_enabled());
   return nested_interrupts_count;
}

/*
 * This sanity check is essential: it assures us that in no case
 * we're running an usermode thread with preemption disabled.
 */
static void DEBUG_check_preemption_enabled_for_usermode(void)
{
   struct task *curr = get_curr_task();
   if (curr && !running_in_kernel(curr) && !nested_interrupts_count) {
      ASSERT(is_preemption_enabled());
   }
}

#else

static ALWAYS_INLINE void DEBUG_check_not_same_interrupt_nested(int n) { }
static ALWAYS_INLINE void DEBUG_check_preemption_enabled_for_usermode(void) { }

#endif // KRN_TRACK_NESTED_INTERR


void irq_entry(regs_t *r)
{
   ASSERT(!are_interrupts_enabled());
   DEBUG_VALIDATE_STACK_PTR();
   ASSERT(get_curr_task() != NULL);
   DEBUG_check_not_same_interrupt_nested(regs_intnum(r));

   handle_irq(r);
}

/*
 * Common fault handler prologue, used only by soft_interrupt_entry().
 *
 * Note: introduced just for symmetry with exit_fault_handler_state(), which
 * is used outside of this file as well.
 */
static void enter_fault_handler_state(int int_num)
{
   push_nested_interrupt(int_num);
   disable_preemption();
}

/*
 * Exit from the fault handler with the correct sequence, the counter-part of
 * enter_fault_handler_state().
 *
 * WARNING: To be used *ONLY* by fault handlers that DO NOT return.
 *
 *    - re-enable the preemption (the last thing disabled)
 *    - pop the last "nested interrupt" caused by the fault
 *
 * See soft_interrupt_entry() for more.
 */

void exit_fault_handler_state(void)
{
   enable_preemption();
   pop_nested_interrupt();
}

void soft_interrupt_entry(regs_t *r)
{
   const int int_num = regs_intnum(r);
   const bool in_syscall = (int_num == SYSCALL_SOFT_INTERRUPT);
   ASSERT(!are_interrupts_enabled());

   if (LIKELY(in_syscall))
      DEBUG_check_preemption_enabled_for_usermode();

   enter_fault_handler_state(int_num);
   enable_interrupts_forced();
   {
      if (LIKELY(in_syscall))
         handle_syscall(r);
      else
         handle_fault(r);
   }
   disable_interrupts_forced();
   exit_fault_handler_state();

   if (LIKELY(in_syscall))
      DEBUG_check_preemption_enabled_for_usermode();
}

