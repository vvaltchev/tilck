
#include <common_defs.h>
#include <process.h>
#include <arch/generic_x86/x86_utils.h>
#include <string_util.h>

extern volatile task_info *current_task;


/*
 * This will keep track of how many ticks that the system
 * has been running for.
 */
volatile u64 jiffies = 0;

volatile u32 disable_preemption_count = 0;

void disable_preemption() {
   disable_preemption_count++;
}

void enable_preemption() {
   ASSERT(disable_preemption_count > 0);
   disable_preemption_count--;
}

bool is_preemption_enabled() {
   return disable_preemption_count == 0;
}

void timer_handler(regs *r)
{
   jiffies++;

   account_ticks();

   /*
    * Here we have to check that disabled_preemption_count is > 1, not > 0
    * since as the way the handle_irq() is implemented, that counter will be
    * always 1 when this function is called. We have avoid calling schedule()
    * if there has been another part of the code that disabled the preemption
    * and we're running in a nested interrupt.
    */
   if (disable_preemption_count > 1) {
      return;
   }

   if (need_reschedule()) {
      disable_preemption_count = 1;
      save_current_task_state(r);
      schedule();
   }
}

