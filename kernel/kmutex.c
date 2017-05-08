
#include <sync.h>
#include <hal.h>
#include <process.h>

volatile uptr new_mutex_id = 0;


void kmutex_init(kmutex *m)
{
   disable_preemption();
   {
      m->id = new_mutex_id++;
      m->owner_task = NULL;
   }
   enable_preemption();
}

void kmutex_destroy(kmutex *m)
{
   (void)m; // Do nothing.
}

void kmutex_lock(kmutex *m)
{
   disable_preemption();

   if (m->owner_task) {

      /*
       * The mutex is NOT recursive: ASSERT that the mutex is not already
       * owned by the current task.
       */
      ASSERT(!kmutex_is_curr_task_holding_lock(m));

      wait_obj_set(&current_task->wobj, WOBJ_KMUTEX, m);
      task_change_state(current_task, TASK_STATE_SLEEPING);

      enable_preemption();
      kernel_yield(); // Go to sleep until someone else holding is the lock.

      // Here for sure this task should hold the mutex.
      ASSERT(kmutex_is_curr_task_holding_lock(m));
      return;

   } else {

      // Nobody owns this mutex, just set the owner
      m->owner_task = get_current_task();
   }

   enable_preemption();
}

bool kmutex_trylock(kmutex *m)
{
   bool success = false;

   disable_preemption();

   if (!m->owner_task) {
      // Nobody owns this mutex, just set the owner
      m->owner_task = get_current_task();
      success = true;
   }

   enable_preemption();
   return success;
}

void kmutex_unlock(kmutex *m)
{
   task_info *pos;

   disable_preemption();
   {
      ASSERT(kmutex_is_curr_task_holding_lock(m));

      m->owner_task = NULL;

      /* Unlock one task waiting to acquire the mutex 'm' */

      list_for_each_entry(pos, &sleeping_tasks_list, sleeping_list) {

         ASSERT(pos->state == TASK_STATE_SLEEPING);

         if (pos->wobj.ptr == m) {
            m->owner_task = pos;
            wait_obj_reset(&pos->wobj);
            task_change_state(pos, TASK_STATE_RUNNABLE);
            break;
         }
      }
   }
   enable_preemption();
}
