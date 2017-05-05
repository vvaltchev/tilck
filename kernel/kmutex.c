
#include <sync.h>
#include <hal.h>
#include <process.h>

volatile uptr new_mutex_id = 0;

struct kmutex {
   uptr id;
   task_info *owner_task;
};


void kmutex_init(kmutex *m)
{
   disable_preemption();
   {
      m->id = new_mutex_id++;
      m->owner_task = NULL;
   }
   enable_preemption();
}

void lock(kmutex *m)
{
   disable_preemption();
   {
      if (m->owner_task) {

         /*
          * The mutex is NOT recursive: ASSERT that the mutex is not already
          * owned by the current task.
          */
         ASSERT(m->owner_task != get_current_task());

         wait_obj_set(&current_task->wobj, WOBJ_KMUTEX, m);
         current_task->state = TASK_STATE_SLEEPING;
         kernel_yield();

      } else {

         // Nobody owns this mutex, just set the owner
         m->owner_task = get_current_task();
      }
   }
   enable_preemption();
}

void unlock(kmutex *m)
{
   task_info *pos;

   disable_preemption();
   {
      ASSERT(m->owner_task == get_current_task());

      m->owner_task = NULL;

      // TODO: make that we iterate only among sleeping tasks

      list_for_each_entry(pos, &tasks_list, list) {
         if (pos->state != TASK_STATE_SLEEPING) {
            continue;
         }

         if (pos->wobj.ptr == m) {
            m->owner_task = pos;
            wait_obj_reset(&pos->wobj);
            pos->state = TASK_STATE_RUNNABLE;
            break;
         }
      }
   }
   enable_preemption();
}
