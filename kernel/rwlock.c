/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/rwlock.h>
#include <tilck/kernel/sched.h>

void rwlock_rp_init(struct rwlock_rp *r)
{
   kmutex_init(&r->readers_lock, 0);
   ksem_init(&r->writers_sem, 1, 1);
   r->readers_count = 0;
   DEBUG_ONLY(r->ex_owner = NULL);
}

void rwlock_rp_destroy(struct rwlock_rp *r)
{
   DEBUG_ONLY(r->ex_owner = NULL);
   r->readers_count = 0;
   ksem_destroy(&r->writers_sem);
   kmutex_destroy(&r->readers_lock);
}

void rwlock_rp_shlock(struct rwlock_rp *r)
{
   kmutex_lock(&r->readers_lock);
   {
      if (++r->readers_count == 1)
         ksem_wait(&r->writers_sem, 1, KSEM_WAIT_FOREVER);
   }
   kmutex_unlock(&r->readers_lock);
}

void rwlock_rp_shunlock(struct rwlock_rp *r)
{
   kmutex_lock(&r->readers_lock);
   {
      if (--r->readers_count == 0)
         ksem_signal(&r->writers_sem, 1);
   }
   kmutex_unlock(&r->readers_lock);
}

void rwlock_rp_exlock(struct rwlock_rp *r)
{
   ksem_wait(&r->writers_sem, 1, KSEM_WAIT_FOREVER);

   ASSERT(r->ex_owner == NULL);
   DEBUG_ONLY(r->ex_owner = get_curr_task());
}

void rwlock_rp_exunlock(struct rwlock_rp *r)
{
   ASSERT(r->ex_owner == get_curr_task());
   DEBUG_ONLY(r->ex_owner = NULL);

   ksem_signal(&r->writers_sem, 1);
}

/* ---------------------------------------------- */

/*
 * Writer-preferring rwlock.
 *
 * Invariant: while any writer is queued or holding (`wq > 0`),
 * incoming readers block on `readers_cv`. The hand-off after a
 * writer release is done explicitly via `writers_cv` (one wake)
 * when more writers are still queued, or via `readers_cv` (wake
 * all) only after the writer queue has fully drained.
 *
 * Two condvars are essential. With a single mixed queue the
 * lock's "write-preferring" semantics would degrade to "depends
 * on whoever wins the kmutex race after exunlock" -- and that
 * race is settled by the scheduler's tiebreak rule, not by this
 * file. The earlier implementation got away with one condvar
 * only because the pre-CFS scheduler gave woken writers a stale-
 * low vruntime advantage; after the wake_vruntime_handoff
 * equalises post-wake vruntime, the tid tiebreak hands the lock
 * to readers on every cycle. Splitting the queues moves the
 * guarantee back into this code.
 */

void rwlock_wp_init(struct rwlock_wp *rw, bool recursive)
{
   kmutex_init(&rw->m, 0);
   kcond_init(&rw->readers_cv);
   kcond_init(&rw->writers_cv);
   rw->ex_owner = NULL;
   rw->r = 0;
   rw->wq = 0;
   rw->rec = recursive;
   rw->rc = 0;
}

void rwlock_wp_destroy(struct rwlock_wp *rw)
{
   ASSERT(rw->ex_owner == NULL);
   ASSERT(rw->r == 0);
   ASSERT(rw->wq == 0);
   kcond_destroy(&rw->writers_cv);
   kcond_destroy(&rw->readers_cv);
   kmutex_destroy(&rw->m);
}

void rwlock_wp_shlock(struct rwlock_wp *rw)
{
   kmutex_lock(&rw->m);
   {
      /*
       * Block while any writer is queued or holding. wq == 0 is
       * the only safe condition to take a shared lock: it means
       * no writer is registered, so we won't starve one by
       * acquiring shared.
       */
      while (rw->wq > 0) {
         kcond_wait(&rw->readers_cv, &rw->m, KCOND_WAIT_FOREVER);
      }
      rw->r++;
   }
   kmutex_unlock(&rw->m);
}

void rwlock_wp_shunlock(struct rwlock_wp *rw)
{
   kmutex_lock(&rw->m);
   {
      /*
       * Decrement readers. When r reaches 0 and there's a writer
       * waiting, hand off to the writer at the head of writers_cv.
       * Do NOT touch readers_cv -- those readers must keep
       * waiting until the writer queue drains.
       */
      if (--rw->r == 0 && rw->wq > 0)
         kcond_signal_one(&rw->writers_cv);
   }
   kmutex_unlock(&rw->m);
}

static void rwlock_wp_exlock_int(struct rwlock_wp *rw)
{
   if (rw->rec) {
      if (rw->ex_owner == get_curr_task()) {
         ASSERT(rw->wq >= 1);
         ASSERT(rw->rc >= 1);
         rw->rc++;
         return;
      }
   }

   /*
    * Register as a writer. From this point new readers block,
    * because shlock checks wq > 0.
    */
   rw->wq++;

   /*
    * Wait until both: (a) no other writer holds the lock
    * (ex_owner == NULL) and (b) no reader holds shlock (r == 0).
    * The shunlock path (when r reaches 0) and the exunlock path
    * (when the current writer releases) signal writers_cv to
    * wake exactly one waiter from the head of this queue, which
    * is FIFO -- so writers are served in arrival order.
    */
   while (rw->ex_owner != NULL || rw->r > 0) {
      kcond_wait(&rw->writers_cv, &rw->m, KCOND_WAIT_FOREVER);
   }

   rw->ex_owner = get_curr_task();

   if (rw->rec) {
      ASSERT(rw->rc == 0);
      rw->rc = 1;
   }
}

void rwlock_wp_exlock(struct rwlock_wp *rw)
{
   kmutex_lock(&rw->m);
   {
      rwlock_wp_exlock_int(rw);
   }
   kmutex_unlock(&rw->m);
}

static void rwlock_wp_exunlock_int(struct rwlock_wp *rw)
{
   ASSERT(rw->ex_owner == get_curr_task());
   ASSERT(rw->wq >= 1);

   if (rw->rec) {
      ASSERT(rw->rc > 0);
      rw->rc--;
      if (rw->rc > 0)
         return;
   }

   rw->ex_owner = NULL;
   rw->wq--;

   /*
    * Hand-off rule: while writers are still in the queue, wake
    * exactly one of them -- they get the lock next, ahead of any
    * reader. Only when the writer queue is fully drained do we
    * wake the (potentially many) readers blocked on readers_cv.
    * This keeps "write-preferring" a property of this code, not
    * of the scheduler.
    */
   if (rw->wq > 0)
      kcond_signal_one(&rw->writers_cv);
   else
      kcond_signal_all(&rw->readers_cv);
}

void rwlock_wp_exunlock(struct rwlock_wp *rw)
{
   kmutex_lock(&rw->m);
   {
      rwlock_wp_exunlock_int(rw);
   }
   kmutex_unlock(&rw->m);
}
