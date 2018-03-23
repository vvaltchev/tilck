
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/process.h>
#include <exos/hal.h>

void handle_syscall(regs *);
void handle_fault(regs *);
void handle_irq(regs *r);

volatile int nested_interrupts_count = 0;
volatile int nested_interrupts[32] = { [0 ... 31] = -1 };

void check_not_in_irq_handler(void)
{
   if (!in_panic) {
      disable_interrupts();
      {
         if (nested_interrupts_count > 0)
            if (is_irq(nested_interrupts[nested_interrupts_count - 1]))
               panic("NOT expected to be in an interrupt handler.");
      }
      enable_interrupts();
   }
}

void push_nested_interrupt(int int_num)
{
   ASSERT(nested_interrupts_count < (int)ARRAY_SIZE(nested_interrupts));
   ASSERT(nested_interrupts_count >= 0);
   nested_interrupts[nested_interrupts_count++] = int_num;
}

void pop_nested_interrupt()
{
   nested_interrupts_count--;
   ASSERT(nested_interrupts_count >= 0);
}

static bool is_same_interrupt_nested(int int_num)
{
   ASSERT(!are_interrupts_enabled());

   for (int i = nested_interrupts_count - 1; i >= 0; i--) {
      if (nested_interrupts[i] == int_num) {
         return true;
      }
   }

   return false;
}

/*
 * This sanity check is very fundamental: it assures us that in no case
 * we're running an usermode thread with preemption disabled.
 */
static ALWAYS_INLINE void DEBUG_check_preemption_enabled_for_usermode()
{
   if (current && !running_in_kernel(current)) {
      if (nested_interrupts_count == 0) {
         ASSERT(is_preemption_enabled());
      }
   }
}


void generic_interrupt_handler(regs *r)
{
   DEBUG_check_disable_interrupts_count_is_0(regs_intnum(r));

   /*
    * We know that interrupts have been disabled exactly once at this point
    * by the CPU or the low-level assembly interrupt handler so we have to
    * set disable_interrupts_count = 1, in order to the counter to be consistent
    * with the actual CPU state.
    */

   disable_interrupts_count = 1;

   ASSERT(!are_interrupts_enabled());
   DEBUG_VALIDATE_STACK_PTR();
   ASSERT(!is_same_interrupt_nested(regs_intnum(r)));
   ASSERT(current != NULL);

   DEBUG_check_preemption_enabled_for_usermode();

   if (is_irq(regs_intnum(r))) {
      handle_irq(r);
      DEBUG_check_preemption_enabled_for_usermode();

      /* Restore the value of disable_interrupts_count to 0. */
      disable_interrupts_count = 0;
      return;
   }

   DEBUG_check_preemption_enabled_for_usermode();

   disable_preemption();
   push_nested_interrupt(regs_intnum(r));

   enable_interrupts_forced();
   ASSERT(are_interrupts_enabled());
   ASSERT(disable_interrupts_count == 0);

   if (LIKELY(regs_intnum(r) == SYSCALL_SOFT_INTERRUPT)) {

      handle_syscall(r);

   } else {

      handle_fault(r);
   }

   enable_preemption();

   DEBUG_check_preemption_enabled_for_usermode();
   pop_nested_interrupt();

   /* Restore the value of disable_interrupts_count to 0. */
   disable_interrupts_count = 0;
}

