/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/rwlock.h>
#include <tilck/kernel/sched.h>

void rwlock_rp_init(rwlock_rp *r)
{
   kmutex_init(&r->readers_lock, 0);
   ksem_init(&r->writers_sem);
   r->readers_count = 0;
   DEBUG_ONLY(r->ex_owner = NULL);
}

void rwlock_rp_destroy(rwlock_rp *r)
{
   DEBUG_ONLY(r->ex_owner = NULL);
   r->readers_count = 0;
   ksem_destroy(&r->writers_sem);
   kmutex_destroy(&r->readers_lock);
}

void rwlock_rp_shlock(rwlock_rp *r)
{
   kmutex_lock(&r->readers_lock);
   {
      if (++r->readers_count == 1)
         ksem_wait(&r->writers_sem);
   }
   kmutex_unlock(&r->readers_lock);
}

void rwlock_rp_shunlock(rwlock_rp *r)
{
   kmutex_lock(&r->readers_lock);
   {
      if (--r->readers_count == 0)
         ksem_signal(&r->writers_sem);
   }
   kmutex_unlock(&r->readers_lock);
}

void rwlock_rp_exlock(rwlock_rp *r)
{
   ksem_wait(&r->writers_sem);

   ASSERT(r->ex_owner == NULL);
   DEBUG_ONLY(r->ex_owner = get_curr_task());
}

void rwlock_rp_exunlock(rwlock_rp *r)
{
   ASSERT(r->ex_owner == get_curr_task());
   DEBUG_ONLY(r->ex_owner = NULL);

   ksem_signal(&r->writers_sem);
}

/* ---------------------------------------------- */

void rwlock_wp_init(rwlock_wp *rw)
{
   kmutex_init(&rw->m, 0);
   kcond_init(&rw->c);
   rw->r = 0;
   rw->w = false;
   DEBUG_ONLY(rw->ex_owner = NULL);
}

void rwlock_wp_destroy(rwlock_wp *rw)
{
   DEBUG_ONLY(rw->ex_owner = NULL);
   rw->w = false;
   rw->r = 0;
   kcond_destory(&rw->c);
   kmutex_destroy(&rw->m);
}

void rwlock_wp_shlock(rwlock_wp *rw)
{
   kmutex_lock(&rw->m);
   {
      /* Wait until there's at least one writer waiting (they have priority) */
      while (rw->w) {
         kcond_wait(&rw->c, &rw->m, KCOND_WAIT_FOREVER);
      }

      /*
       * OK, no writer is waiting and we're holding the mutex: we can safely
       * increment the readers count and claim that the acquired a shared
       * lock.
       */
      rw->r++;
   }
   kmutex_unlock(&rw->m);
}

void rwlock_wp_shunlock(rwlock_wp *rw)
{
   kmutex_lock(&rw->m);
   {
      /*
       * Decrement the readers count and, in case there no more readers, signal
       * the condition in order to wake-up the writers are waiting on it.
       */
      if (--rw->r == 0)
         kcond_signal_one(&rw->c);
   }
   kmutex_unlock(&rw->m);
}

void rwlock_wp_exlock(rwlock_wp *rw)
{
   kmutex_lock(&rw->m);
   {
      /* Wait our turn until other writers are waiting to write */
      while (rw->w) {
         kcond_wait(&rw->c, &rw->m, KCOND_WAIT_FOREVER);
      }

      /*
       * OK, no writer is waiting to write and we're holding the mutex: now
       * it's our turn to wait on the condition `readers > 0`.
       */
      rw->w = true;

      /* Wait until there are any readers currently holding the rwlock */
      while (rw->r > 0) {
         kcond_wait(&rw->c, &rw->m, KCOND_WAIT_FOREVER);
      }

      /*
       * No more readers: great. Now we're really holding an exclusive access to
       * the rwlock. New readers cannot acquire a shared lock because `w` is set
       * and other writes cannot acquire it, for the same reason.
       */

      ASSERT(rw->ex_owner == NULL);
      DEBUG_ONLY(rw->ex_owner = get_curr_task());
   }
   kmutex_unlock(&rw->m);
}

void rwlock_wp_exunlock(rwlock_wp *rw)
{
   kmutex_lock(&rw->m);
   {
      ASSERT(rw->ex_owner == get_curr_task());
      DEBUG_ONLY(rw->ex_owner = NULL);

      /* The `w` flag must be set */
      ASSERT(rw->w);

      /* Unset the `w` flag (no more writers) */
      rw->w = false;

      /* Signal all the readers potentially waiting on the condition */
      kcond_signal_all(&rw->c);
   }
   kmutex_unlock(&rw->m);
}
