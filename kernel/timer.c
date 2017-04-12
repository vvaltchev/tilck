
#include <common_defs.h>
#include <process.h>
#include <arch/generic_x86/utils.h>
#include <string_util.h>

extern volatile task_info *current_task;


/*
 * This will keep track of how many ticks that the system
 * has been running for.
 */
volatile u64 jiffies = 0;

u32 disabled_preemption_jiffies = 0;

void disable_preemption_for(int jiffies)
{
   disabled_preemption_jiffies += jiffies;
}

void timer_handler(regs *r)
{
   jiffies++;

   if (disabled_preemption_jiffies != 0) {
      disabled_preemption_jiffies--;
      return;
   }

   if (!current_task) {
      // The kernel is still initializing and we cannot call schedule() yet.
      return;
   }

   save_current_task_state(r);
   schedule();
}

