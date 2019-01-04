/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/atomics.h>

#include <tilck/kernel/sync.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/interrupts.h>

static ATOMIC(uptr) new_mutex_id = 1;

void wait_obj_set(wait_obj *wo,
                  enum wo_type type,
                  void *ptr,
                  list_node *wait_list)
{
   atomic_store_explicit(&wo->__ptr, ptr, mo_relaxed);

   disable_preemption();
   {
      wo->type = type;

      ASSERT(list_is_null(&wo->wait_list_node) ||
            list_is_empty(&wo->wait_list_node));

      list_node_init(&wo->wait_list_node);

      if (wait_list)
         list_add_tail(wait_list, &wo->wait_list_node);
   }
   enable_preemption();
}

void *wait_obj_reset(wait_obj *wo)
{
   void *oldp = atomic_exchange_explicit(&wo->__ptr, (void*)NULL, mo_relaxed);
   disable_preemption();
   {
      wo->type = WOBJ_NONE;

      if (list_is_node_in_list(&wo->wait_list_node)) {
         list_remove(&wo->wait_list_node);
      }

      list_node_init(&wo->wait_list_node);
   }
   enable_preemption();
   return oldp;
}

void task_set_wait_obj(task_info *ti,
                       enum wo_type type,
                       void *ptr,
                       list_node *wait_list)
{
   wait_obj_set(&ti->wobj, type, ptr, wait_list);
   task_change_state(ti, TASK_STATE_SLEEPING);
}

bool kmutex_is_curr_task_holding_lock(kmutex *m)
{
   return m->owner_task == get_curr_task();
}

void kmutex_init(kmutex *m, u32 flags)
{
   bzero(m, sizeof(kmutex));
   m->id = atomic_fetch_add_explicit(&new_mutex_id, 1U, mo_relaxed);
   m->flags = flags;
   list_node_init(&m->wait_list);
}

void kmutex_destroy(kmutex *m)
{
   bzero(m, sizeof(kmutex)); // NOTE: !id => invalid kmutex
}

void kmutex_lock(kmutex *m)
{
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

   task_set_wait_obj(get_curr_task(), WOBJ_KMUTEX, m, &m->wait_list);
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
   bool success = false;

   DEBUG_ONLY(check_not_in_irq_handler());
   disable_preemption();

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

   enable_preemption();
   return success;
}

void kmutex_unlock(kmutex *m)
{
   DEBUG_ONLY(check_not_in_irq_handler());
   disable_preemption();

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

   /* Unlock one task waiting to acquire the mutex 'm' (if any) */
   if (!list_is_empty(&m->wait_list)) {

      wait_obj *task_wo =
         list_first_obj(&m->wait_list, wait_obj, wait_list_node);

      task_info *ti = CONTAINER_OF(task_wo, task_info, wobj);

      m->owner_task = ti;

      if (m->flags & KMUTEX_FL_RECURSIVE)
         m->lock_count++;

      ASSERT(ti->state == TASK_STATE_SLEEPING);
      wait_obj_reset(task_wo);
      task_change_state(ti, TASK_STATE_RUNNABLE);

   } // if (!list_is_empty(&m->wait_list))

   enable_preemption();
}
