/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/sched.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/self_tests.h>

#define THREAD_COUNT                       (2)

static struct kcond cond = { 0 };
static struct kmutex cond_mutex = { 0 };
static ATOMIC(int) waiters_ready;

static void kcond_thread_test(void *arg)
{
   const int tn = (int)(ulong)arg;
   kmutex_lock(&cond_mutex);

   printk("[thread %i]: under lock, waiting for signal..\n", tn);

   /*
    * Increment under cond_mutex BEFORE kcond_wait. kcond_wait() adds
    * curr to the cond's wait_list before releasing the mutex, so when
    * the signal generator next acquires the mutex and reads
    * waiters_ready, every counted thread is guaranteed to be on the
    * wait_list. Without this barrier, the signal generator could
    * signal_all and exit before a slow-to-start waiter ever calls
    * kcond_wait(), leaving that waiter blocked forever.
    */
   atomic_fetch_add_explicit(&waiters_ready, 1, mo_relaxed);

   bool success = kcond_wait(&cond, &cond_mutex, KCOND_WAIT_FOREVER);

   if (success)
      printk("[thread %i]: under lock, signal received..\n", tn);
   else
      panic("[thread %i]: under lock, kcond_wait() FAILED\n", tn);

   kmutex_unlock(&cond_mutex);

   printk("[thread %i]: exit\n", tn);
}

static void kcond_thread_wait_ticks(void *unused)
{
   kmutex_lock(&cond_mutex);
   printk("[kcond wait ticks]: holding the lock, run wait()\n");

   bool success = kcond_wait(&cond, &cond_mutex, KRN_TIMER_HZ/2);

   if (!success)
      printk("[kcond wait ticks]: woke up due to timeout, as expected!\n");
   else
      panic("[kcond wait ticks] FAILED: kcond_wait() returned true.");

   kmutex_unlock(&cond_mutex);
}


static void kcond_thread_signal_generator(void *unused)
{
   int tid;

   /*
    * Wait until both kcond_thread_test instances are queued on the cond.
    * We acquire the mutex to read waiters_ready in the same critical
    * section that the waiters use to enqueue themselves; without this,
    * a waiter could still be blocked at kmutex_lock() when we signal,
    * miss the broadcast, and then wait forever in kcond_wait().
    */
   while (true) {
      kmutex_lock(&cond_mutex);
      {
         if (atomic_load_explicit(&waiters_ready, mo_relaxed) == THREAD_COUNT)
            break;
      }
      kmutex_unlock(&cond_mutex);
      kernel_sleep(1);
   }

   printk("[thread signal]: both waiters queued, signal_all!\n");

   kcond_signal_all(&cond);
   kmutex_unlock(&cond_mutex);

   printk("[thread signal]: exit\n");

   printk("Run thread kcond_thread_wait_ticks\n");

   if ((tid = kthread_create(&kcond_thread_wait_ticks, 0, NULL)) < 0)
      panic("Unable to create a thread for kcond_thread_wait_ticks()");

   kthread_join(tid, true);
}

void selftest_kcond()
{
   int tids[THREAD_COUNT + 1];
   kmutex_init(&cond_mutex, 0);
   kcond_init(&cond);
   atomic_store_explicit(&waiters_ready, 0, mo_relaxed);

   for (u32 i = 0; i < THREAD_COUNT; i++) {
      tids[i] = kthread_create(&kcond_thread_test, 0, TO_PTR(i + 1));
      VERIFY(tids[i] > 0);
   }

   tids[THREAD_COUNT] = kthread_create(&kcond_thread_signal_generator, 0, NULL);
   VERIFY(tids[THREAD_COUNT] > 0);

   kthread_join_all(tids, ARRAY_SIZE(tids), true);
   kcond_destroy(&cond);
   se_regular_end();
}

REGISTER_SELF_TEST(kcond, se_short, &selftest_kcond)
