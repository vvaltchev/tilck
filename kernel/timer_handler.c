/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/atomics.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/elf_utils.h>

volatile u64 __ticks; /* ticks since the timer started */
ATOMIC(u32) disable_preemption_count = 1;

static list_node timer_wakeup_list = make_list_node(timer_wakeup_list);

void task_set_wakeup_timer(task_info *ti, u64 ticks)
{
   uptr var;
   ASSERT(ticks > 0);

   if (atomic_exchange_explicit(&ti->ticks_before_wake_up,
                                ticks, memory_order_relaxed) == 0)
   {
      disable_interrupts(&var);

      if (!list_is_node_in_list(&ti->wakeup_timer_node))
         list_add_tail(&timer_wakeup_list, &ti->wakeup_timer_node);

      enable_interrupts(&var);
   }
}

void task_update_wakeup_timer_if_any(task_info *ti, u64 new_ticks)
{
   u64 curr;
   ASSERT(new_ticks > 0);

   do {

      curr = ti->ticks_before_wake_up;

      if (curr == 0)
         break; // we do NOTHING if there is NO current wake-up timer.

   } while (!atomic_compare_exchange_weak_explicit(&ti->ticks_before_wake_up,
                                                   &curr,
                                                   new_ticks,
                                                   memory_order_relaxed,
                                                   memory_order_relaxed));
}

void task_cancel_wakeup_timer(task_info *ti)
{
   atomic_store_explicit(&ti->ticks_before_wake_up, 0, memory_order_relaxed);
}

static task_info *tick_all_timers(void)
{
   task_info *pos, *temp;
   task_info *last_ready_task = NULL;
   uptr var;


   list_for_each(pos, temp, &timer_wakeup_list, wakeup_timer_node) {

      disable_interrupts(&var);

      if (pos->ticks_before_wake_up == 0) {

         list_remove(&pos->wakeup_timer_node);

      } else if (--pos->ticks_before_wake_up == 0) {

         list_remove(&pos->wakeup_timer_node);

         if (pos->state == TASK_STATE_SLEEPING) {
            task_change_state(pos, TASK_STATE_RUNNABLE);
            last_ready_task = pos;
         }
      }

      enable_interrupts(&var);
   }

   return last_ready_task;
}

void kernel_sleep(u64 ticks)
{
   DEBUG_ONLY(check_not_in_irq_handler());

   if (ticks) {
      task_set_wakeup_timer(get_curr_task(), ticks);
      task_change_state(get_curr_task(), TASK_STATE_SLEEPING);
   }

   kernel_yield();
}

#if KERNEL_TRACK_NESTED_INTERRUPTS
   static u32 slow_timer_irq_handler_count = 0;

   void print_slow_timer_irq_handler_counter(void)
   {
      printk("slow_timer_irq_handler_counter: %u\n",
             slow_timer_irq_handler_count);
   }
#endif


void debug_check_tasks_lists(void)
{
   task_info *pos, *temp;
   ptrdiff_t off;
   const char *what_str = "?";
   uptr var;

   disable_interrupts(&var);

   list_for_each(pos, temp, &sleeping_tasks_list, sleeping_node) {

      if (pos->state != TASK_STATE_SLEEPING) {

         if (is_kernel_thread(pos))
            what_str = find_sym_at_addr_safe((uptr)pos->what, &off, NULL);

         panic("%s task %d [w: %s] in the sleeping_tasks_list with state: %d",
               is_kernel_thread(pos) ? "kernel" : "user",
               pos->tid, what_str, pos->state);
      }
   }

   enable_interrupts(&var);
}

int timer_irq_handler(regs *context)
{
#if KERNEL_TRACK_NESTED_INTERRUPTS
   if (in_nested_irq0()) {
      slow_timer_irq_handler_count++;
      return 0;
   }
#endif

   __ticks++;
   account_ticks();
   task_info *last_ready_task = tick_all_timers();

   /*
    * Here we have to check that disabled_preemption_count is > 1, not > 0
    * since as the way the handle_irq() is implemented, that counter will be
    * always 1 when this function is called. We must not call schedule()
    * if there has been another part of the code that disabled the preemption.
    */
   if (disable_preemption_count > 1) {
      return 0;
   }

   ASSERT(disable_preemption_count == 1); // again, for us disable = 1 means 0.

   DEBUG_ONLY(debug_check_tasks_lists());

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
      disable_interrupts(&var); /* under #if KERNEL_TRACK_NESTED_INTERRUPTS */
      int c = get_nested_interrupts_count();
      ASSERT(c == 1 || (c == 2 && in_syscall()));
      enable_interrupts(&var);
   }
#endif

   if (last_ready_task) {

      if (get_curr_task()->state == TASK_STATE_RUNNING) {
         task_change_state(get_curr_task(), TASK_STATE_RUNNABLE);
      }

      save_current_task_state(context);
      switch_to_task(last_ready_task, X86_PC_TIMER_IRQ);
   }

   if (need_reschedule()) {
      save_current_task_state(context);
      schedule(X86_PC_TIMER_IRQ);
   }

   return 0;
}

void init_timer(void)
{
   timer_set_freq(TIMER_HZ);
   irq_install_handler(X86_PC_TIMER_IRQ, timer_irq_handler);
}
