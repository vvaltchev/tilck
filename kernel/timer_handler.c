
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/process.h>
#include <exos/hal.h>
#include <exos/irq.h>

/*
 * This will keep track of how many ticks that the system
 * has been running for.
 */
volatile u64 jiffies;

volatile u32 disable_preemption_count = 1;

typedef struct {

   u64 ticks_to_sleep;
   task_info *task;     // task == NULL means that the slot is unused.

} kthread_timer_sleep_obj;

kthread_timer_sleep_obj timers_array[64];

/*
 * TODO: consider making this logic reentrant: avoid disabling interrupts
 * and disable just the preemption.
 */

int set_task_to_wake_after(task_info *task, u64 ticks)
{
   uptr var;
   disable_interrupts(&var);
   {
      for (uptr i = 0; i < ARRAY_SIZE(timers_array); i++) {
         if (!timers_array[i].task) {
            timers_array[i].ticks_to_sleep = ticks;
            timers_array[i].task = task;
            task_change_state(get_current_task(), TASK_STATE_SLEEPING);
            return i;
         }
      }
   }
   enable_interrupts(&var);

   // TODO: consider implementing a fallback here. For example use a linkedlist.
   panic("Unable to find a free slot in timers_array.");
}

void cancel_timer(int timer_num)
{
   uptr var;
   disable_interrupts(&var);
   {
      ASSERT(timers_array[timer_num].task != NULL);
      timers_array[timer_num].task = NULL;
   }
   enable_interrupts(&var);
}

static task_info *tick_all_timers(void *context)
{
   task_info *last_ready_task = NULL;

   for (uptr i = 0; i < ARRAY_SIZE(timers_array); i++) {

      if (!timers_array[i].task)
         continue;

      if (--timers_array[i].ticks_to_sleep == 0) {
         last_ready_task = timers_array[i].task;

         /* In no case a sleeping task could go to kernel and get here */
         ASSERT(get_current_task() != last_ready_task);

         timers_array[i].task = NULL;
         task_change_state(last_ready_task, TASK_STATE_RUNNABLE);
      }
   }

   return last_ready_task;
}

void kernel_sleep(u64 ticks)
{
   set_task_to_wake_after(current, ticks);
   kernel_yield();
}


void timer_handler(void *context)
{
   jiffies++;

   account_ticks();
   task_info *last_ready_task = tick_all_timers(context);

   // [DEBUG] Useful to trigger nested printk calls
   // if (!(jiffies % 100)) {
   //    printk("[TIMER TICK]\n");
   // }

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

   ASSERT(disable_preemption_count == 1); // again, for us disable = 1 means 0.

   /*
    * We CANNOT allow the timer to call the scheduler if it interrupted an
    * interrupt handler. Interrupt handlers MUST always to run with preemption
    * disabled.
    *
    * Therefore, the ASSERT checks that:
    *
    * nested_interrupts_count == 1
    *     meaning the timer is the only current interrupt: a kernel or an user
    *     task was running regularly.
    *
    * OR
    *
    * nested_interrupts_count == 2
    *     meaning that the timer interrupted a syscall working with preemption
    *     enabled.
    */

#ifdef DEBUG
   {
      uptr var;
      disable_interrupts(&var);
      int c = get_nested_interrupts_count();
      ASSERT(c == 1 || (c == 2 && in_syscall()));
      enable_interrupts(&var);
   }
#endif

   if (last_ready_task) {
      ASSERT(current->state == TASK_STATE_RUNNING);
      task_change_state(current, TASK_STATE_RUNNABLE);
      save_current_task_state(context);
      switch_to_task(last_ready_task);
   }

   if (need_reschedule()) {
      save_current_task_state(context);
      schedule();
   }
}

