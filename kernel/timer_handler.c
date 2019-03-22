/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/atomics.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/tasklet.h>

u64 __ticks; /* ticks since the timer started */

#if KERNEL_TRACK_NESTED_INTERRUPTS
u32 slow_timer_irq_handler_count;
#endif

static list timer_wakeup_list = make_list(timer_wakeup_list);

void task_set_wakeup_timer(task_info *ti, u32 ticks)
{
   uptr var;
   ASSERT(ticks > 0);

   if (atomic_exchange_explicit(&ti->ticks_before_wake_up,
                                ticks, mo_relaxed) == 0)
   {
      disable_interrupts(&var);

      if (!list_is_node_in_list(&ti->wakeup_timer_node))
         list_add_tail(&timer_wakeup_list, &ti->wakeup_timer_node);

      enable_interrupts(&var);
   }
}

void task_update_wakeup_timer_if_any(task_info *ti, u32 new_ticks)
{
   u32 curr;
   ASSERT(new_ticks > 0);

   do {

      curr = ti->ticks_before_wake_up;

      if (curr == 0)
         break; // we do NOTHING if there is NO current wake-up timer.

   } while (!atomic_cas_weak(&ti->ticks_before_wake_up,
                             &curr, new_ticks, mo_relaxed, mo_relaxed));
}

u32 task_cancel_wakeup_timer(task_info *ti)
{
   return atomic_exchange_explicit(&ti->ticks_before_wake_up, 0, mo_relaxed);
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

   /*
    * Implementation: why
    * ---------------------
    *
    * This function was previously implemented as just:
    *
    *    if (ticks) {
    *       task_set_wakeup_timer(get_curr_task(), ticks);
    *       task_change_state(get_curr_task(), TASK_STATE_SLEEPING);
    *    }
    *    kernel_yield();
    *
    * And it worked, but it required task_info->ticks_before_wake_up to be
    * actually 64-bit wide, which that lead to inefficiency of 32-bit systems,
    * in particular because 'ticks_before_wake_up' is now atomic and it's used
    * atomically even when we don't want to (see tick_all_timers()). Pointer
    * size relaxed atomics are pretty cheap, but double-pointer size are not.
    * In order to use a 32-bit value for 'ticks_before_wake_up' and, at the same
    * time being able to sleep for more than 2^32-1 ticks, we needed a more
    * tricky implementation (below).
    *
    * Implementation: how
    * ----------------------
    *
    * The simpler way to explain the algorithm is to just assume everything
    * is in base 10 and that ticks_before_wake_up has 2 digits, while we want
    * to support 4 digits sleep time. For example, we want to sleep for 234
    * ticks. The algorithm first computes 534 % 100 = 34 and then 534 / 100 = 5.
    * After that, it sleeps q (= 5) times for 99 ticks (max allowed). Clearly,
    * we missed 5 ticks (5 * 99 < 500) this way, but we'll going to fix that
    * buy just sleeping 'q' ticks. Thus, by now, we've slept for 500 ticks.
    * Now we have to sleep for 34 ticks more are we're done.
    *
    * The same logic applies to base-2 case with 32-bit and 64-bit integers,
    * just the numbers are much bigger. The remainder can be computed using
    * a bitmask, while the division by using just a right shift.
    */

   const u32 rem = ticks & 0xffffffff;
   const u32 q = ticks >> 32;

   for (u32 i = 0; i < q; i++) {
      task_set_wakeup_timer(get_curr_task(), 0xffffffff);
      task_change_state(get_curr_task(), TASK_STATE_SLEEPING);
      kernel_yield();
   }

   if (q) {
      task_set_wakeup_timer(get_curr_task(), q);
      task_change_state(get_curr_task(), TASK_STATE_SLEEPING);

      if (rem) {
         /* Yield only if we're going to sleep again because rem > 0 */
         kernel_yield();
      }
   }

   if (rem) {
      task_set_wakeup_timer(get_curr_task(), rem);
      task_change_state(get_curr_task(), TASK_STATE_SLEEPING);
   }

   /* We must yield at least once, even if ticks == 0 */
   kernel_yield();
}

static ALWAYS_INLINE void debug_timer_irq_sanity_checks(void)
{
   /*
    * We CANNOT allow the timer to call the scheduler if it interrupted an
    * interrupt handler. Interrupt handlers MUST always to run with preemption
    * disabled.
    *
    * Therefore, the ASSERT below checks that:
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
}

static ALWAYS_INLINE bool timer_nested_irq(void)
{

#if KERNEL_TRACK_NESTED_INTERRUPTS

   uptr var;
   disable_interrupts(&var); /* under #if KERNEL_TRACK_NESTED_INTERRUPTS */

   if (in_nested_irq_num(X86_PC_TIMER_IRQ)) {
      slow_timer_irq_handler_count++;
      enable_interrupts(&var);
      return true;
   }

   enable_interrupts(&var);

#endif

   return false;
}

enum irq_action timer_irq_handler(regs *context)
{
   if (KERNEL_TRACK_NESTED_INTERRUPTS)
      if (timer_nested_irq())
         return IRQ_FULLY_HANDLED;

   /*
    * It is SAFE to directly increase the 64-bit integer __ticks here, without
    * disabling the interrupts and without trying to use any kind of atomic
    * operation because this is the ONLY place where __ticks is modified. Even
    * when nested IRQ0 is allowed, __ticks won't be touched by the nested
    * handler as you can see above.
    */
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
      return IRQ_FULLY_HANDLED;
   }

   ASSERT(disable_preemption_count == 1); // again, for us disable = 1 means 0.
   debug_timer_irq_sanity_checks();

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

   return IRQ_FULLY_HANDLED;
}

static irq_handler_node timer_irq_handler_node = {
   .node = make_list_node(timer_irq_handler_node.node),
   .handler = timer_irq_handler
};

void init_timer(void)
{
   timer_set_freq(TIMER_HZ);
   irq_install_handler(X86_PC_TIMER_IRQ, &timer_irq_handler_node);
}
