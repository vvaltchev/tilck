
#include <exos/sync.h>
#include <exos/hal.h>
#include <exos/process.h>

static uptr new_mutex_id;

inline bool kmutex_is_curr_task_holding_lock(kmutex *m)
{
   return m->owner_task == get_current_task();
}

void kmutex_init(kmutex *m)
{
   m->owner_task = NULL;
   m->id = ATOMIC_FETCH_AND_ADD(&new_mutex_id, 1);
}

void kmutex_destroy(kmutex *m)
{
   (void)m; // Do nothing.
}

void kmutex_lock(kmutex *m)
{
   DEBUG_ONLY(check_not_in_irq_handler());
   disable_preemption();

   if (m->owner_task) {

      /*
       * The mutex is NOT recursive: ASSERT that the mutex is not already
       * owned by the current task.
       */
      ASSERT(!kmutex_is_curr_task_holding_lock(m));

      wait_obj_set(&get_current_task()->wobj, WOBJ_KMUTEX, m);
      task_change_state(get_current_task(), TASK_STATE_SLEEPING);

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

   DEBUG_ONLY(check_not_in_irq_handler());
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

   DEBUG_ONLY(check_not_in_irq_handler());
   disable_preemption();
   {
      ASSERT(kmutex_is_curr_task_holding_lock(m));

      m->owner_task = NULL;

      /* Unlock one task waiting to acquire the mutex 'm' */

      list_for_each(pos, &sleeping_tasks_list, sleeping_list) {

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
