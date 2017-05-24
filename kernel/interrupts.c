
#include <common_defs.h>
#include <hal.h>
#include <string_util.h>


volatile int nested_interrupts_count = 0;
volatile int nested_interrupts[32] = { [0 ... 31] = -1 };

void handle_syscall(regs *);
void handle_fault(regs *);
void handle_irq(regs *r);



void pop_nested_interrupt()
{
   // int curr_int = get_curr_interrupt();

   // if (is_irq(curr_int)) {
   //    PIC_sendEOI(curr_int - 32);
   // }


   nested_interrupts_count--;
   ASSERT(nested_interrupts_count >= 0);


   // if (LIKELY(current != NULL)) {

   //    nested_interrupts_count--;
   //    ASSERT(nested_interrupts_count >= 0);

   // } else if (nested_interrupts_count > 0) {

   //    nested_interrupts_count--;
   // }
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

DEBUG_ONLY(extern volatile bool in_page_fault);

extern volatile u64 jiffies;

void generic_interrupt_handler(regs *r)
{
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
            r->int_num, disable_int_c, jiffies);
   }

   /*
    * We know that interrupts have been disabled exactly once at this point
    * by the CPU or the low-level assembly interrupt handler so we have to
    * set disable_interrupts_count = 1, in order to the counter to be consistent
    * with the actual CPU state.
    */

   disable_interrupts_count = 1;

   ASSERT(!are_interrupts_enabled());

   task_info *curr = get_current_task();

   DEBUG_VALIDATE_STACK_PTR();

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

   enable_interrupts_forced();
   ASSERT(are_interrupts_enabled());
   ASSERT(disable_interrupts_count == 0);

   if (LIKELY(r->int_num == SYSCALL_SOFT_INTERRUPT)) {

      handle_syscall(r);

   } else {

      handle_fault(r);
   }

   enable_preemption();

   if (curr && !curr->running_in_kernel) {
      ASSERT(nested_interrupts_count > 0 || is_preemption_enabled());
   }

   pop_nested_interrupt();
}

