
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/process.h>
#include <exos/hal.h>
#include <exos/irq.h>

/*
 * This will keep track of how many ticks that the system has been running for.
 */
volatile u64 jiffies;

volatile u32 disable_preemption_count = 1;

typedef struct {

   u64 ticks_to_sleep;
   task_info *task;     // task == NULL means that the slot is unused.

} kthread_timer_sleep_obj;

kthread_timer_sleep_obj timers_array[64];

int set_task_to_wake_after(task_info *task, u64 ticks)
{
   ASSERT(!in_irq());

   for (uptr i = 0; i < ARRAY_SIZE(timers_array); i++) {
      if (BOOL_COMPARE_AND_SWAP(&timers_array[i].task, NULL, task)) {
         timers_array[i].ticks_to_sleep = ticks;
         task_change_state(get_curr_task(), TASK_STATE_SLEEPING);
         return i;
      }
   }

   // TODO: consider implementing a fallback here. For example use a linkedlist.
   panic("Unable to find a free slot in timers_array.");
}

void cancel_timer(int timer_num)
{
   ASSERT(timers_array[timer_num].task != NULL);
   timers_array[timer_num].task = NULL;
}

static task_info *tick_all_timers(void)
{
   task_info *last_ready_task = NULL;

   for (uptr i = 0; i < ARRAY_SIZE(timers_array); i++) {

      if (!timers_array[i].task)
         continue;

      if (--timers_array[i].ticks_to_sleep == 0) {
         last_ready_task = timers_array[i].task;

         /* In no case a sleeping task could go to kernel and get here */
         ASSERT(get_curr_task() != last_ready_task);

         timers_array[i].task = NULL;
         task_change_state(last_ready_task, TASK_STATE_RUNNABLE);
      }
   }

   return last_ready_task;
}

void kernel_sleep(u64 ticks)
{
   set_task_to_wake_after(get_curr_task(), ticks);
   kernel_yield();
}


void timer_handler(void *context)
{
   jiffies++;

   account_ticks();
   task_info *last_ready_task = tick_all_timers();

   // [DEBUG] Useful to trigger nested printk calls
   // if (!(jiffies % 40)) {
   //    printk("[TIMER TICK]\n");
   // }

   /*
    * Here we have to check that disabled_preemption_count is > 1, not > 0
    * since as the way the handle_irq() is implemented, that counter will be
    * always 1 when this function is called. We must not call schedule()
    * if there has been another part of the code that disabled the preemption.
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

#if defined(DEBUG) && KERNEL_TRACK_NESTED_INTERRUPTS
   {
      uptr var;
      disable_interrupts(&var);
      int c = get_nested_interrupts_count();
      ASSERT(c == 1 || (c == 2 && in_syscall()));
      enable_interrupts(&var);
   }
#endif

   if (last_ready_task) {
      ASSERT(get_curr_task()->state == TASK_STATE_RUNNING);
      task_change_state(get_curr_task(), TASK_STATE_RUNNABLE);
      save_current_task_state(context);
      switch_to_task(last_ready_task);
   }

   if (need_reschedule()) {
      save_current_task_state(context);
      schedule();
   }
}

