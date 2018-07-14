
#include <exos/kernel/sync.h>
#include <exos/kernel/hal.h>
#include <exos/kernel/process.h>

static uptr new_cond_id;

void kcond_init(kcond *c)
{
   c->id = ATOMIC_FETCH_AND_ADD(&new_cond_id, 1);
}

bool kcond_wait(kcond *c, kmutex *m, u32 timeout_ticks)
{
   disable_preemption();
   DEBUG_ONLY(check_not_in_irq_handler());
   ASSERT(!m || kmutex_is_curr_task_holding_lock(m));

   task_info *curr = get_curr_task();
   wait_obj_set(&curr->wobj, WOBJ_KCOND, c);

   if (timeout_ticks != KCOND_WAIT_FOREVER) {
      c->timer_num = set_task_to_wake_after(curr, timeout_ticks);
   } else {
      c->timer_num = -1;
      task_change_state(curr, TASK_STATE_SLEEPING);
   }

   if (m) {
      kmutex_unlock(m);
   }

   enable_preemption();
   kernel_yield(); // Go to sleep until a signal is fired or timeout happens.

   if (m) {
      kmutex_lock(m); // Re-acquire the lock back
   }

   /*
    * If a signal really woke up this task, then wobj.ptr must be NULL.
    * If it isn't NULL, that means that the task has been weaken up because
    * the timeout expired.
    */
   return !curr->wobj.ptr;
}

void kcond_signal_single(kcond *c, task_info *ti)
{
   ASSERT(!is_preemption_enabled());

   if (ti->state != TASK_STATE_SLEEPING) {
      /* the signal is lost, that's typical for conditions */
      return;
   }

   if (c->timer_num >= 0)
      cancel_timer(c->timer_num, ti);

   wait_obj_reset(&ti->wobj);
   task_change_state(ti, TASK_STATE_RUNNABLE);
}

void kcond_signal_int(kcond *c, bool all)
{
   task_info *pos;
   disable_preemption();

   list_for_each(pos, &sleeping_tasks_list, sleeping_list) {

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
