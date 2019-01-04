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
   list_node_init(&c->wait_list);
}

bool kcond_wait(kcond *c, kmutex *m, u32 timeout_ticks)
{
   DEBUG_ONLY(check_not_in_irq_handler());
   ASSERT(!m || kmutex_is_curr_task_holding_lock(m));
   task_info *curr = get_curr_task();

   disable_preemption();
   {
      wait_obj_set(&curr->wobj, WOBJ_KCOND, c, &c->wait_list);

      if (timeout_ticks != KCOND_WAIT_FOREVER)
         task_set_wakeup_timer(curr, timeout_ticks);

      task_change_state(curr, TASK_STATE_SLEEPING);

      if (m) {
         kmutex_unlock(m);
      }
   }
   enable_preemption();
   kernel_yield(); // Go to sleep until a signal is fired or timeout happens.

   if (m) {
      kmutex_lock(m); // Re-acquire the lock back
   }

   /*
    * wait_obj_reset() returns the older value of wobj.ptr: in case it was
    * NULL, we'll return true (no timeout). In case it was != NULL, we woke up
    * because of the timeout -> return false.
    */
   return !wait_obj_reset(&curr->wobj);
}

void kcond_signal_single(kcond *c, task_info *ti)
{
   DEBUG_ONLY(check_not_in_irq_handler());

   if (ti->state != TASK_STATE_SLEEPING) {
      /* the signal is lost, that's typical for conditions */
      return;
   }

   task_cancel_wakeup_timer(ti);
   wait_obj_reset(&ti->wobj);
   task_change_state(ti, TASK_STATE_RUNNABLE);
}

void kcond_signal_int(kcond *c, bool all)
{
   wait_obj *wo_pos, *temp;
   DEBUG_ONLY(check_not_in_irq_handler());

   disable_preemption();

   list_for_each(wo_pos, temp, &c->wait_list, wait_list_node) {

      task_info *ti = CONTAINER_OF(wo_pos, task_info, wobj);
      kcond_signal_single(c, ti);

      if (!all)
         break;
   }

   enable_preemption();
}


void kcond_destory(kcond *c)
{
   (void) c; // do nothing.
}
