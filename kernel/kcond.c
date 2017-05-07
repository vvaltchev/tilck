
#include <sync.h>
#include <hal.h>
#include <process.h>

volatile uptr new_cond_id = 0;

void kcond_init(kcond *c)
{
   disable_preemption();
   {
      c->id = new_cond_id++;
   }
   enable_preemption();
}

void kcond_wait(kcond *c, kmutex *m)
{
   disable_preemption();
   ASSERT(kmutex_is_curr_task_holding_lock(m));

   wait_obj_set(&current_task->wobj, WOBJ_KCOND, c);
   current_task->state = TASK_STATE_SLEEPING;

   kmutex_unlock(m);
   enable_preemption();
   kernel_yield(); // Go to sleep until someone else holding is the lock.
}

void kcond_signal_int(kcond *c, bool all)
{
   task_info *pos;

   disable_preemption();

   // TODO: make that we iterate only among sleeping tasks

   list_for_each_entry(pos, &tasks_list, list) {

      if (pos->state != TASK_STATE_SLEEPING) {
         continue;
      }

      if (pos->wobj.ptr != c) {
         continue;
      }

      // pos->wobj.ptr == c

      wait_obj_reset(&pos->wobj);
      pos->state = TASK_STATE_RUNNABLE;

      if (!all) {
         break;
      }
   }

   enable_preemption();
}


void kcond_destory(kcond *c)
{
   (void) c; // do nothing.
}
