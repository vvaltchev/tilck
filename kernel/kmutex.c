/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/sync.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/sched.h>

bool kmutex_is_curr_task_holding_lock(struct kmutex *m)
{
   return m->owner_task == get_curr_task();
}

void kmutex_init(struct kmutex *m, u32 flags)
{
   DEBUG_ONLY(check_not_in_irq_handler());
   bzero(m, sizeof(struct kmutex));
   m->flags = flags;
   list_init(&m->wait_list);
}

void kmutex_destroy(struct kmutex *m)
{
   bzero(m, sizeof(struct kmutex));
}

static ALWAYS_INLINE void
kmutex_lock_enable_preemption_wrapper(struct kmutex *m)
{
#if KERNEL_SELFTESTS

   if (LIKELY(!(m->flags & KMUTEX_FL_ALLOW_LOCK_WITH_PREEMPT_DISABLED))) {
      enable_preemption();       /* default case */
   } else {
      ASSERT(disable_preemption_count == 2);
      force_enable_preemption(); /* special case only for self tests */
   }

#else

   enable_preemption(); /* the only case when self tests don't exist */

#endif
}

void kmutex_lock(struct kmutex *m)
{
   disable_preemption();
   DEBUG_ONLY(check_not_in_irq_handler());

   if (!m->owner_task) {

      /* Nobody owns this mutex, just make this task own it */
      m->owner_task = get_curr_task();

      if (m->flags & KMUTEX_FL_RECURSIVE) {
         ASSERT(m->lock_count == 0);
         m->lock_count++;
      }

      kmutex_lock_enable_preemption_wrapper(m);
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

#if KMUTEX_STATS_ENABLED
   m->num_waiters++;
   m->max_num_waiters = MAX(m->num_waiters, m->max_num_waiters);
#endif

   task_set_wait_obj(get_curr_task(), WOBJ_KMUTEX, m, &m->wait_list);
   kmutex_lock_enable_preemption_wrapper(m);
   kernel_yield(); // Go to sleep until someone else is holding the lock.

   /* ------------------- We've been woken up ------------------- */

#if KMUTEX_STATS_ENABLED
   m->num_waiters--;
#endif

   /* Now for sure this task should hold the mutex */
   ASSERT(kmutex_is_curr_task_holding_lock(m));

   /*
    * DEBUG check: in case we went to sleep with a recursive mutex, then the
    * lock_count must be just 1 now.
    */
   if (m->flags & KMUTEX_FL_RECURSIVE) {
      ASSERT(m->lock_count == 1);
   }
}

bool kmutex_trylock(struct kmutex *m)
{
   bool success = false;

   disable_preemption();
   DEBUG_ONLY(check_not_in_irq_handler());

   if (!m->owner_task) {

      /* Nobody owns this mutex, just make this task own it */
      m->owner_task = get_curr_task();
      success = true;

      if (m->flags & KMUTEX_FL_RECURSIVE)
         m->lock_count++;

   } else {

      /*
       * There IS an owner task, but we can still acquire the mutex if:
       *    - the mutex is recursive
       *    - the task holding it is actually the current task
       */

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

void kmutex_unlock(struct kmutex *m)
{
   disable_preemption();

   DEBUG_ONLY(check_not_in_irq_handler());
   ASSERT(kmutex_is_curr_task_holding_lock(m));

   if (m->flags & KMUTEX_FL_RECURSIVE) {

      ASSERT(m->lock_count > 0);

      if (--m->lock_count > 0) {
         enable_preemption();
         return;
      }

      // m->lock_count == 0: we have to really unlock the mutex
   }

   m->owner_task = NULL;

   /* Unlock one task waiting to acquire the mutex 'm' (if any) */
   if (!list_is_empty(&m->wait_list)) {

      struct wait_obj *task_wo =
         list_first_obj(&m->wait_list, struct wait_obj, wait_list_node);

      struct task *ti = CONTAINER_OF(task_wo, struct task, wobj);

      m->owner_task = ti;

      if (m->flags & KMUTEX_FL_RECURSIVE)
         m->lock_count++;

      ASSERT(ti->state == TASK_STATE_SLEEPING);
      task_reset_wait_obj(ti);

   } // if (!list_is_empty(&m->wait_list))

   enable_preemption();
}
