/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/self_tests.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/sync.h>

static struct kcond conds[2];
static atomic_int_t mobj_se_test_signal_counter;
static bool mobj_se_test_assumption_failed;

static void mobj_waiter_sig_thread(void *arg)
{
   ulong n = (ulong) arg;
   u64 ticks_to_sleep = (u64)(n + 1) * KRN_TIMER_HZ / 2;

   printk("[thread %lu] sleep for %" PRIu64 " ticks\n", n, ticks_to_sleep);
   kernel_sleep(ticks_to_sleep);

   printk("[thread %lu] signal cond %ld\n", n, n);
   kcond_signal_one(&conds[n]);
   atomic_fetch_add_int(&mobj_se_test_signal_counter, 1);
}

static void mobj_waiter_wait_thread(void *arg)
{
   printk("[wait th] Start\n");

   if (atomic_load_int(&mobj_se_test_signal_counter) > 0) {
      printk("[wait th] Test timing assumption failed, re-try\n");
      mobj_se_test_assumption_failed = true;
      return;
   }

   struct multi_obj_waiter *w = allocate_mobj_waiter(ARRAY_SIZE(conds));
   VERIFY(w != NULL);

   for (int j = 0; j < ARRAY_SIZE(conds); j++)
      mobj_waiter_set(w, j, WOBJ_KCOND, &conds[j], &conds[j].wait_list);

   for (int i = 0; i < ARRAY_SIZE(conds); i++) {

      printk("[wait th]: going to sleep on waiter obj\n");

      disable_preemption();
      prepare_to_wait_on_multi_obj(w);
      enter_sleep_wait_state();

      printk("[wait th ] wake up #%u\n", i);

      for (int j = 0; j < w->count; j++) {

         struct mwobj_elem *me = &w->elems[j];

         if (me->type && !me->wobj.type) {
            printk("[wait th ]    -> condition #%u was signaled\n", j);
            mobj_waiter_reset(me);
         }
      }
   }

   free_mobj_waiter(w);
}

void selftest_mobj_waiter()
{
   int tids[ARRAY_SIZE(conds)];
   int w_tid;

retry:

   atomic_store_int(&mobj_se_test_signal_counter, 0);
   mobj_se_test_assumption_failed = false;

   for (int i = 0; i < ARRAY_SIZE(conds); i++) {
      kcond_init(&conds[i]);
      tids[i] = kthread_create(&mobj_waiter_sig_thread, 0, TO_PTR(i));
      VERIFY(tids[i] > 0);
   }

   w_tid = kthread_create(&mobj_waiter_wait_thread, 0, NULL);
   VERIFY(w_tid > 0);

   kthread_join_all(tids, ARRAY_SIZE(tids), true);
   kthread_join(w_tid, true);

   for (int i = 0; i < ARRAY_SIZE(conds); i++)
      kcond_destroy(&conds[i]);

   if (mobj_se_test_assumption_failed)
      goto retry;

   se_regular_end();
}

REGISTER_SELF_TEST(mobj_waiter, se_short, &selftest_mobj_waiter)

/* -------------------------------------------------- */
/*       multi-obj waiter: signaled_list / rearm      */
/* -------------------------------------------------- */

/*
 * Exercise the signaled_list bookkeeping that poll()/select() rely on to
 * survive a "wake up but nothing's ready" iteration.
 *
 * The check is synchronous on purpose — the test task itself drives every
 * step, so there's no scheduling race to worry about. The kcond_signal_int
 * MWO_ELEM path doesn't require `ti` to be SLEEPING; it only wakes the task
 * if it is, and otherwise just records the signal. That lets us "fire" a
 * signal from the same task that owns the waiter, observe the resulting
 * list state, rearm, and fire again.
 *
 * The property under test: after the rearm helper drains signaled_list, the
 * fired elem is back in its kcond's wait_list — so the NEXT signal on that
 * kcond reaches us instead of falling on an empty list. Without this, the
 * poll/select continue-and-resleep path is deaf to follow-up signals.
 */

static struct kcond rearm_test_cond_a;
static struct kcond rearm_test_cond_b;

void selftest_mobj_waiter_rearm()
{
   struct multi_obj_waiter *w;

   kcond_init(&rearm_test_cond_a);
   kcond_init(&rearm_test_cond_b);

   w = allocate_mobj_waiter(2);
   VERIFY(w != NULL);

   mobj_waiter_set(w, 0, WOBJ_KCOND,
                   &rearm_test_cond_a, &rearm_test_cond_a.wait_list);
   mobj_waiter_set(w, 1, WOBJ_KCOND,
                   &rearm_test_cond_b, &rearm_test_cond_b.wait_list);

   /* Baseline: both elems registered, signaled_list empty. */
   VERIFY(!list_is_empty(&rearm_test_cond_a.wait_list));
   VERIFY(!list_is_empty(&rearm_test_cond_b.wait_list));
   VERIFY(list_is_empty(&w->signaled_list));

   /* Signal A: elem 0 moves to signaled_list, elem 1 is untouched. */
   kcond_signal_one(&rearm_test_cond_a);
   VERIFY(list_is_empty(&rearm_test_cond_a.wait_list));
   VERIFY(!list_is_empty(&rearm_test_cond_b.wait_list));
   VERIFY(!list_is_empty(&w->signaled_list));

   /* Signal B too: both elems are now in signaled_list. */
   kcond_signal_one(&rearm_test_cond_b);
   VERIFY(list_is_empty(&rearm_test_cond_a.wait_list));
   VERIFY(list_is_empty(&rearm_test_cond_b.wait_list));
   VERIFY(!list_is_empty(&w->signaled_list));

   /*
    * Rearm: signaled_list should drain, and each elem should be back in
    * the kcond it was originally registered against. This is exactly what
    * poll()/select() do before a re-sleep when the predicate re-check
    * comes back empty — the property we're guarding against regressing.
    */
   disable_preemption();
   mobj_waiter_rearm_signaled(w);
   enable_preemption();

   VERIFY(!list_is_empty(&rearm_test_cond_a.wait_list));
   VERIFY(!list_is_empty(&rearm_test_cond_b.wait_list));
   VERIFY(list_is_empty(&w->signaled_list));

   /* And a follow-up signal on A finds the rearmed elem. */
   kcond_signal_one(&rearm_test_cond_a);
   VERIFY(list_is_empty(&rearm_test_cond_a.wait_list));
   VERIFY(!list_is_empty(&w->signaled_list));

   free_mobj_waiter(w);
   kcond_destroy(&rearm_test_cond_a);
   kcond_destroy(&rearm_test_cond_b);

   se_regular_end();
}

REGISTER_SELF_TEST(mobj_waiter_rearm, se_short, &selftest_mobj_waiter_rearm)
