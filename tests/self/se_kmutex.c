/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/self_tests.h>
#include <tilck/kernel/timer.h>

#define KMUTEX_SEK_TH_ITERS 100000

static kmutex test_mutex;
static int sek_vars[3];
static const int sek_set_1[3] = {1, 2, 3};
static const int sek_set_2[3] = {10, 20, 30};

static void sek_set_vars(const int *set)
{
   for (u32 i = 0; i < ARRAY_SIZE(sek_vars); i++) {
      sek_vars[i] = set[i];
      kernel_yield();
   }
}

static void sek_check_set_eq(const int *set)
{
   for (u32 i = 0; i < ARRAY_SIZE(sek_vars); i++) {
      VERIFY(sek_vars[i] == set[i]);
      kernel_yield();
   }
}

static void sek_thread(void *unused)
{
   for (int iter = 0; iter < KMUTEX_SEK_TH_ITERS; iter++) {

      kmutex_lock(&test_mutex);
      {
         kernel_yield();

         if (sek_vars[0] == sek_set_1[0]) {

            sek_check_set_eq(sek_set_1);
            sek_set_vars(sek_set_2);

         } else {

            sek_check_set_eq(sek_set_2);
            sek_set_vars(sek_set_1);
         }

         kernel_yield();
      }
      kmutex_unlock(&test_mutex);

   } // for (int iter = 0; iter < KMUTEX_SEK_TH_ITERS; iter++)
}

void selftest_kmutex_med()
{
   int tids[3];
   kmutex_init(&test_mutex, 0);
   sek_set_vars(sek_set_1);

   for (int i = 0; i < 3; i++) {
      tids[i] = kthread_create(sek_thread, NULL);
      VERIFY(tids[i] > 0);
   }

   kthread_join_all(tids, ARRAY_SIZE(tids));

   kmutex_destroy(&test_mutex);
   regular_self_test_end();
}

/* -------------------------------------------------- */
/*               Recursive mutex test                 */
/* -------------------------------------------------- */

static void test_kmutex_thread(void *arg)
{
   printk("%i) before lock\n", arg);

   kmutex_lock(&test_mutex);

   printk("%i) under lock..\n", arg);

   for (u32 i = 0; i < 128*MB; i++) {
      asmVolatile("nop");
   }

   kmutex_unlock(&test_mutex);

   printk("%i) after lock\n", arg);
}

static void test_kmutex_thread_trylock()
{
   printk("3) before trylock\n");

   bool locked = kmutex_trylock(&test_mutex);

   if (locked) {

      printk("3) trylock SUCCEEDED: under lock..\n");

      kmutex_unlock(&test_mutex);

      printk("3) after lock\n");

   } else {
      printk("3) trylock returned FALSE\n");
   }
}

void selftest_kmutex_rec_med()
{
   bool success;
   int tids[3];

   printk("kmutex recursive test\n");
   kmutex_init(&test_mutex, KMUTEX_FL_RECURSIVE);

   kmutex_lock(&test_mutex);
   printk("Locked once\n");

   kmutex_lock(&test_mutex);
   printk("Locked twice\n");

   success = kmutex_trylock(&test_mutex);

   if (!success) {
      panic("kmutex_trylock() failed on the same thread");
   }

   printk("Locked 3 times (last with trylock)\n");

   tids[0] = kthread_create(test_kmutex_thread_trylock, NULL);
   VERIFY(tids[0] > 0);
   kthread_join(tids[0]);

   kmutex_unlock(&test_mutex);
   printk("Unlocked once\n");

   kmutex_unlock(&test_mutex);
   printk("Unlocked twice\n");

   kmutex_unlock(&test_mutex);
   printk("Unlocked 3 times\n");

   tids[0] = kthread_create(&test_kmutex_thread, (void*) 1);
   VERIFY(tids[0] > 0);

   tids[1] = kthread_create(&test_kmutex_thread, (void*) 2);
   VERIFY(tids[1] > 0);

   tids[2] = kthread_create(&test_kmutex_thread_trylock, NULL);
   VERIFY(tids[2] > 0);

   kthread_join_all(tids, ARRAY_SIZE(tids));
   kmutex_destroy(&test_mutex);
   regular_self_test_end();
}

/* -------------------------------------------------- */
/*               Strong order test                    */
/* -------------------------------------------------- */

/*
 * HOW IT WORKS
 * --------------
 *
 * The idea is to check that our kmutex implementation behaves like a strong
 * binary semaphore. In other words, if given task A tried to acquire the mutex
 * M before any given task B, on unlock() it MUST BE woken up and hold the mutex
 * BEFORE task B does.
 *
 * In order to do that, we create many threads and substantially make each one
 * of them to try to acquire the test_mutex. At the end, we would like to verify
 * that they acquired the mutex *in order*. But, what does *in order* mean?
 * How we do know which is the correct order? The creation of threads does NOT
 * have any order. For example: thread B, created AFTER thread A, may run before
 * it. Well, in order to do that, we use another mutex, called `order_mutex`.
 * Threads first get any order using `order_mutex` and then, in that order, the
 * try to acquire `test_mutex`. Of course, threads might be so fast that each
 * thread just acquires and releases both the mutex without being preempted and
 * no thread really sleeps on kmutex_lock(). In order to prevent that, we sleep
 * while holding the `test_mutex`. For a better understanding, see the comments
 * below.
 */

static int tids[128];
static int tid_by_idx1[128];
static int tid_by_idx2[128];
static int idx1, idx2;

static kmutex order_mutex;

static void kmutex_ord_th()
{
   u64 ticks;
   int tid = get_curr_task()->tid;

   /*
    * Since in practice, currently on Tilck, threads are executed pretty much
    * in the same order as they're created, we use the HACK below in order to
    * kind-of randomize the moment when they actually acquire the order_mutex,
    * simulating the general case where the `order_mutex` is strictly required.
    */
   kernel_sleep( ((u32)tid / sizeof(void *)) % 7 );

   kmutex_lock(&order_mutex);
   {
      tid_by_idx1[idx1++] = tid;
      ticks = get_ticks();        /* save `ticks` in order to check it later */
   }
   kmutex_unlock(&order_mutex);

   if (get_ticks() != ticks) {

      /*
       * We've been preempted and we won't be able to call kmutex_lock()
       * immediately after recording our "order" with the `order_mutex`: this
       * is pretty unlikely to happen, but it can. Therefore, just register -1
       * as tid, considering this thread "unlucky".
       *
       * NOTE: this trick does NOT detect preemption in 100% of the cases, as
       * we might be preempted just after this check passed, before calling
       * kmutex_lock(). And we really do not have at the moment a workaround for
       * that because we cannot call kmutex_lock() with preemption disabled.
       * Just, this HACK reduces as much as possible the likelyhood of missing
       * a preemption between `order_mutex` and `test_mutex`, which defeats the
       * purpose of the `order_mutex`.
       */
      tid = -1;
   }

   kmutex_lock(&test_mutex);
   {
      tid_by_idx2[idx2++] = tid;

      /*
       * After registering this thread at position `idx2`, now sleep for 1 tick
       * WHILE holding the lock, in order to force all the other tasks to sleep
       * on kmutex_lock(), creating a queue. This another trick necessary to
       * check that strong ordering actually occurs. Without it, threads might
       * be so fast that they just:
       *
       *    - acquire & release the order mutex without sleeping
       *    - acquire & release the test mutex without sleeping
       *
       * Therefore, the whole test will be pointless. Now instead, when
       * KMUTEX_STATS_ENABLED is 1, we can check that the order_mutex has
       * typically max_num_waiters = 0, while the test mutex has max_num_waiters
       * equals to almost its maxiumum (127). Typically, it's ~122.
       */

      kernel_sleep(1);
   }
   kmutex_unlock(&test_mutex);
}

void selftest_kmutex_ord_med()
{
   u32 unlucky_threads = 0;
   int tid;

   idx1 = idx2 = 0;
   kmutex_init(&test_mutex, 0);
   kmutex_init(&order_mutex, 0);

   for (u32 i = 0; i < ARRAY_SIZE(tids); i++) {

      if ((tid = kthread_create(&kmutex_ord_th, NULL)) < 0)
         panic("[selftest] Unable to create kthread for kmutex_ord_th()");

      tids[i] = tid;
   }

   kthread_join_all(tids, ARRAY_SIZE(tids));

#if KMUTEX_STATS_ENABLED
   printk("order_mutex max waiters: %u\n", order_mutex.max_num_waiters);
   printk("test_mutex max waiters:  %u\n", test_mutex.max_num_waiters);
   VERIFY(test_mutex.max_num_waiters > 0);
#endif

   for (u32 i = 0; i < ARRAY_SIZE(tids); i++) {

      int t1, t2;

      t1 = tid_by_idx1[i];
      t2 = tid_by_idx2[i];

      if (t2 < 0) {
         unlucky_threads++;
         continue;
      }

      if (t2 != t1) {
         panic("kmutex strong order test failed");
      }
   }

   if (unlucky_threads > 0) {

      if (unlucky_threads > ARRAY_SIZE(tids) / 2)
         panic("Too many unlucky threads");

      printk("[selftests] Note: there were %u/%u unlucky threads",
             unlucky_threads, ARRAY_SIZE(tids));
   }

   kmutex_destroy(&order_mutex);
   kmutex_destroy(&test_mutex);
   regular_self_test_end();
}
