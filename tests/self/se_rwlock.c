/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/rwlock.h>
#include <tilck/kernel/sched.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/self_tests.h>
#include <tilck/kernel/timer.h>

#define RWLOCK_RP_TH_ITERS    10000
#define RWLOCK_RP_READERS     5
#define RWLOCK_RP_WRITERS     5

static rwlock_rp test_rwlrp;
static int sekrp_vars[3];
static const int sek_rp_set_1[3] = {1, 2, 3};
static const int sek_rp_set_2[3] = {10, 20, 30};
static ATOMIC(int) readers_running;
static ATOMIC(int) writers_running;

static void sek_set_vars(const int *set)
{
   for (u32 i = 0; i < ARRAY_SIZE(sekrp_vars); i++) {
      sekrp_vars[i] = set[i];
      kernel_yield();
   }
}

static void sek_check_set_eq(const int *set)
{
   for (u32 i = 0; i < ARRAY_SIZE(sekrp_vars); i++) {
      VERIFY(sekrp_vars[i] == set[i]);
      kernel_yield();
   }
}

static void sek_rp_read_thread(void *unused)
{
   readers_running++;

   for (int iter = 0; iter < RWLOCK_RP_TH_ITERS; iter++) {

      rwlock_rp_shlock(&test_rwlrp);
      {
         if (sekrp_vars[0] == sek_rp_set_1[0])
            sek_check_set_eq(sek_rp_set_1);
         else
            sek_check_set_eq(sek_rp_set_2);
      }
      rwlock_rp_shunlock(&test_rwlrp);
   }

   readers_running--;
}

static void sek_rp_write_thread(void *unused)
{
   writers_running++;

   for (int iter = 0; iter < RWLOCK_RP_TH_ITERS; iter++) {

      rwlock_rp_exlock(&test_rwlrp);
      {
         kernel_yield();

         if (sekrp_vars[0] == sek_rp_set_1[0]) {

            sek_check_set_eq(sek_rp_set_1);
            sek_set_vars(sek_rp_set_2);

         } else {

            sek_check_set_eq(sek_rp_set_2);
            sek_set_vars(sek_rp_set_1);
         }

         kernel_yield();
      }
      rwlock_rp_exunlock(&test_rwlrp);
   }

   writers_running--;
}

static void se_rwlock_rp_common(int *rt, int *wt)
{
   sek_set_vars(sek_rp_set_1);

   for (u32 i = 0; i < RWLOCK_RP_READERS; i++) {
      rt[i] = kthread_create(sek_rp_read_thread, NULL);
      VERIFY(rt[i] > 0);
   }

   for (u32 i = 0; i < RWLOCK_RP_WRITERS; i++) {
      wt[i] = kthread_create(sek_rp_write_thread, NULL);
      VERIFY(wt[i] > 0);
   }
}

void selftest_rwlock_rp_short()
{
   int rt[RWLOCK_RP_READERS];
   int wt[RWLOCK_RP_WRITERS];

   rwlock_rp_init(&test_rwlrp);

   /*
    * Since we're testing a read-preferring rwlock, we except that, after all
    * readers have finished, there will still be writers running. At the same
    * way, we expect that, if we join first the writers, there won't be any
    * running readers.
    */

   se_rwlock_rp_common(rt, wt);
   kthread_join_all(rt, ARRAY_SIZE(rt));
   printk("After readers, running writers: %d\n", writers_running);
   VERIFY(writers_running > 0);
   kthread_join_all(wt, ARRAY_SIZE(wt));

   se_rwlock_rp_common(rt, wt);
   kthread_join_all(wt, ARRAY_SIZE(wt));
   printk("After writers, running readers: %d\n", readers_running);
   VERIFY(readers_running == 0);
   kthread_join_all(rt, ARRAY_SIZE(rt));

   rwlock_rp_destroy(&test_rwlrp);
   regular_self_test_end();
}
