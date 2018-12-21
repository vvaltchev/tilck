/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/atomics.h>

#include <tilck/kernel/sync.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/interrupts.h>

static ATOMIC(uptr) new_cond_id = 1;

void kcond_init(kcond *c)
{
   c->id = atomic_fetch_add_explicit(&new_cond_id, 1U, mo_relaxed);
}

bool kcond_wait(kcond *c, kmutex *m, u32 timeout_ticks)
{
   disable_preemption();
   DEBUG_ONLY(check_not_in_irq_handler());
   ASSERT(!m || kmutex_is_curr_task_holding_lock(m));

   uptr var;
   task_info *curr = get_curr_task();

   disable_interrupts(&var);
   {
      wait_obj_set(&curr->wobj, WOBJ_KCOND, c);

      if (timeout_ticks != KCOND_WAIT_FOREVER)
         task_set_wakeup_timer(curr, timeout_ticks);

      task_change_state(curr, TASK_STATE_SLEEPING);
   }
   enable_interrupts(&var);

   if (m) {
      kmutex_unlock(m);
   }

   enable_preemption();
   kernel_yield(); // Go to sleep until a signal is fired or timeout happens.

   if (m) {
      kmutex_lock(m); // Re-acquire the lock back
   }

   if (curr->wobj.ptr) {

      /*
       * The task has been weaken up because the timeout expired. Clearly,
       * the 'wobj' has not been reset.
       */

      wait_obj_reset(&curr->wobj);
      return false;
   }

   /* Default case: the condition has been signalled */
   return true;
}

void kcond_signal_single(kcond *c, task_info *ti)
{
   ASSERT(!is_preemption_enabled());

   if (ti->state != TASK_STATE_SLEEPING) {
      /* the signal is lost, that's typical for conditions */
      return;
   }

   uptr var;
   disable_interrupts(&var);
   {
      task_cancel_wakeup_timer(ti);
      wait_obj_reset(&ti->wobj);
      task_change_state(ti, TASK_STATE_RUNNABLE);
   }
   enable_interrupts(&var);
}

void kcond_signal_int(kcond *c, bool all)
{
   task_info *pos, *temp;
   disable_preemption();

   list_for_each(pos, temp, &sleeping_tasks_list, sleeping_node) {

      if (pos->wobj.ptr == c) {

         kcond_signal_single(c, pos);

         if (!all)
            break;
      }
   }

   enable_preemption();
}


void kcond_destory(kcond *c)
{
   (void) c; // do nothing.
}
