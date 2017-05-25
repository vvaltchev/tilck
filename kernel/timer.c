
#include <common_defs.h>
#include <process.h>
#include <hal.h>
#include <irq.h>

/*
 * This will keep track of how many ticks that the system
 * has been running for.
 */
volatile u64 jiffies = 0;

volatile u32 disable_preemption_count = 1;

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

typedef struct {

   u64 ticks_to_sleep;
   task_info *task;     // where NULL, the slot is unused.

} kthread_timer_sleep_obj;

kthread_timer_sleep_obj timers_array[64] = { 0 };

/*
 * TODO: consider making this logic reentrant: avoid disabling interrupts
 * and disable just the preemption.
 */

static void register_curr_task_for_timer_sleep(u64 ticks)
{
   ASSERT(!are_interrupts_enabled());

   for (uptr i = 0; i < ARRAY_SIZE(timers_array); i++) {
      if (!timers_array[i].task) {
         timers_array[i].ticks_to_sleep = ticks;
         timers_array[i].task = get_current_task();
         task_change_state(get_current_task(), TASK_STATE_SLEEPING);
         return;
      }
   }

   NOT_REACHED(); // TODO: fallback to a linked list here
}

static task_info *tick_all_timers(void *context)
{
   task_info *last_ready_task = NULL;

   for (uptr i = 0; i < ARRAY_SIZE(timers_array); i++) {

      if (!timers_array[i].task) {
         continue;
      }

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
   disable_interrupts();
   register_curr_task_for_timer_sleep(ticks);
   enable_interrupts();

   kernel_yield();
}


void timer_handler(void *context)
{
   jiffies++;

   account_ticks();
   task_info *last_ready_task = tick_all_timers(context);

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
    * interrupt handler. Interrupt handlers have always to run with preemption
    * disabled.
    *
    * Therefore, the ASSERT checks that:
    *
    * nested_interrupts_count == 1
    *     meaning the timer is the only current interrupt because a kernel
    *     thread was running)
    *
    * OR
    *
    * nested_interrupts_count == 2
    *     meaning that the timer interrupted a syscall working with preemption
    *     enabled.
    */

   ASSERT(nested_interrupts_count == 1 ||
          (nested_interrupts_count == 2 && in_syscall()));

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

