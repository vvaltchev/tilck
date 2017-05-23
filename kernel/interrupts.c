
#include <common_defs.h>
#include <hal.h>
#include <string_util.h>


volatile int nested_interrupts_count = 0;
volatile int nested_interrupts[32] = { [0 ... 31] = -1 };

void handle_syscall(regs *);
void handle_fault(regs *);
void handle_irq(regs *r);



void end_current_interrupt_handling()
{
   int curr_int = get_curr_interrupt();

   if (is_irq(curr_int)) {
      PIC_sendEOI(curr_int - 32);
   }

   if (LIKELY(current != NULL)) {

      nested_interrupts_count--;
      ASSERT(nested_interrupts_count >= 0);

   } else if (nested_interrupts_count > 0) {

      nested_interrupts_count--;
   }
}

bool is_interrupt_racing_with_itself(int int_num) {

   for (int i = nested_interrupts_count - 1; i >= 0; i--) {
      if (nested_interrupts[i] == int_num) {
         return true;
      }
   }

   return false;
}

void push_nested_interrupt(int int_num)
{
   nested_interrupts[nested_interrupts_count++] = int_num;
}

DEBUG_ONLY(extern volatile bool in_page_fault;)

void generic_interrupt_handler(regs *r)
{
   /*
    * We know that interrupts have been disabled exactly once at this point,
    * so, we're forcing disable_interrupts_count = 1.
    */
   disable_interrupts_count = 1;

   // This ASSERT is pretty heavy to check, keeping commented.
   // ASSERT(!are_interrupts_enabled());

   task_info *curr = get_current_task();

   validate_stack_pointer();

   ASSERT(nested_interrupts_count < (int)ARRAY_SIZE(nested_interrupts));
   ASSERT(!is_interrupt_racing_with_itself(r->int_num));

   if (is_irq(r->int_num)) {
      handle_irq(r);
      return;
   }

   if (curr && !curr->running_in_kernel) {
      ASSERT(nested_interrupts_count > 0 || is_preemption_enabled());
   }

   disable_preemption();
   push_nested_interrupt(r->int_num);

   // Re-enable the interrupts, for the same reason as before.
   enable_interrupts_forced();

   if (LIKELY(r->int_num == SYSCALL_SOFT_INTERRUPT)) {

      handle_syscall(r);

   } else {

      handle_fault(r);
   }

   enable_preemption();

   if (curr && !curr->running_in_kernel) {
      ASSERT(nested_interrupts_count > 0 || is_preemption_enabled());
   }

   end_current_interrupt_handling();
}

