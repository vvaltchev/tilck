/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/atomics.h>

#include <tilck/kernel/sync.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/sched.h>

static ATOMIC(uptr) new_mutex_id = 1;

bool kmutex_is_curr_task_holding_lock(kmutex *m)
{
   return m->owner_task == get_curr_task();
}

void kmutex_init(kmutex *m, u32 flags)
{
   bzero(m, sizeof(kmutex));
   m->id = atomic_fetch_add_explicit(&new_mutex_id, 1U, mo_relaxed);
   m->flags = flags;
}

void kmutex_destroy(kmutex *m)
{
   bzero(m, sizeof(kmutex)); // NOTE: !id => invalid kmutex
}

void kmutex_lock(kmutex *m)
{
   uptr var;

   DEBUG_ONLY(check_not_in_irq_handler());
   disable_preemption();

   if (!m->owner_task) {
      // Nobody owns this mutex, just set the owner
      m->owner_task = get_curr_task();

      if (m->flags & KMUTEX_FL_RECURSIVE)
         m->lock_count++;

      enable_preemption();
      return;
   }

   if (m->flags & KMUTEX_FL_RECURSIVE) {

      ASSERT(m->lock_count > 0);

      if (kmutex_is_curr_task_holding_lock(m)) {
         m->lock_count++;
         enable_preemption();
         return;
      }

   } else {
      ASSERT(!kmutex_is_curr_task_holding_lock(m));
   }

   disable_interrupts(&var);
   {
      wait_obj_set(&get_curr_task()->wobj, WOBJ_KMUTEX, m);
      task_change_state(get_curr_task(), TASK_STATE_SLEEPING);
   }
   enable_interrupts(&var);

   enable_preemption();
   kernel_yield(); // Go to sleep until someone else is holding the lock.

   // Now for sure this task should hold the mutex.
   ASSERT(kmutex_is_curr_task_holding_lock(m));

   if (m->flags & KMUTEX_FL_RECURSIVE) {
      ASSERT(m->lock_count == 1);
   }
}

bool kmutex_trylock(kmutex *m)
{
   uptr var;
   bool success = false;

   DEBUG_ONLY(check_not_in_irq_handler());
   disable_interrupts(&var);

   if (!m->owner_task) {

      // Nobody owns this mutex, just set the owner
      m->owner_task = get_curr_task();
      success = true;

      if (m->flags & KMUTEX_FL_RECURSIVE)
         m->lock_count++;

   } else {

      // There is an owner task

      if (m->flags & KMUTEX_FL_RECURSIVE) {
         if (kmutex_is_curr_task_holding_lock(m)) {
            m->lock_count++;
            success = true;
         }
      }
   }

   enable_interrupts(&var);
   return success;
}

void kmutex_unlock(kmutex *m)
{
   uptr var;
   task_info *pos, *temp;

   DEBUG_ONLY(check_not_in_irq_handler());
   disable_preemption();
   {
      ASSERT(kmutex_is_curr_task_holding_lock(m));

      if (m->flags & KMUTEX_FL_RECURSIVE) {

         ASSERT(m->lock_count > 0);

         if (--m->lock_count > 0) {
            enable_preemption();
            return;
         }

         // m->lock_count == 0, we have to really unlock the mutex
      }

      m->owner_task = NULL;

      /* Unlock one task waiting to acquire the mutex 'm' */

      list_for_each(pos, temp, &sleeping_tasks_list, sleeping_node) {

         ASSERT(pos->state == TASK_STATE_SLEEPING);

         if (pos->wobj.ptr != m)
            continue;

         disable_interrupts(&var);

         // 2nd check for pos->wobj.ptr == m, but with interrupts disabled
         if (pos->wobj.ptr == m) {

            m->owner_task = pos;

            if (m->flags & KMUTEX_FL_RECURSIVE)
               m->lock_count++;

            wait_obj_reset(&pos->wobj);
            task_change_state(pos, TASK_STATE_RUNNABLE);
            enable_interrupts(&var);
            break;
         }

         enable_interrupts(&var);
      }
   }
   enable_preemption();
}
