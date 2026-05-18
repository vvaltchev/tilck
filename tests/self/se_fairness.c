/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Fairness selftest: empirical check that the CFS-style scheduler
 * gives roughly equal CPU shares to N equal-weight CPU-bound kernel
 * threads over a fixed window.
 *
 * The threads synchronize at a startup barrier so the measurement
 * window is the same for every participant, then each runs a busy
 * loop that polls a stop flag. The coordinator sleeps for
 * FAIRNESS_DURATION_TICKS, then asks every thread to exit. Each
 * thread snapshots its own ticks.total before and after the work
 * loop; the coordinator reads (end - start) after kthread_join_all
 * returns (so the snapshots are stable and the task structs are
 * still alive long enough for the snapshot writes to land).
 *
 * Pre-CFS the runnable list would have favored whichever task had
 * the lowest accumulated `total` (the previous selection key);
 * after CFS step 5/6/7 with vruntime + dynamic slice, fairness is
 * the explicit goal of the design. This test guards that contract
 * against future regressions.
 */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/atomics.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/self_tests.h>

#define FAIRNESS_THREADS         5
#define FAIRNESS_DURATION_TICKS  (KRN_TIMER_HZ * 2)   /* 2 seconds */

/*
 * Tolerated absolute spread between min and max per-thread tick
 * counts, expressed as a percentage of the mean. Empirically the
 * scheduler delivers ~3% spread on the default config, so 10% is a
 * comfortable headroom while still catching real regressions.
 *
 * The whole measurement is in scheduler ticks (kernel_sleep is
 * tick-driven, ticks.total is tick-driven), so a slow CI VM that
 * starves the vCPU just dilates the wall-clock duration of the
 * test without affecting the ratio between participants. The
 * tolerance doesn't need slack for that.
 */
#define FAIRNESS_TOLERANCE_PCT   10

static atomic_int_t fairness_stop_flag;
static atomic_int_t fairness_ready_count;
static u64 fairness_start_ticks[FAIRNESS_THREADS];
static u64 fairness_end_ticks[FAIRNESS_THREADS];

static void fairness_thread(void *arg)
{
   const int idx = (int)(ulong)arg;
   ulong var;

   /* Barrier: wait until every participant has been scheduled at
    * least once, so they all start measuring from the same point. */
   atomic_fetch_add(&fairness_ready_count, 1);
   while (atomic_load(&fairness_ready_count) < FAIRNESS_THREADS)
      kernel_yield();

   /*
    * Read ticks.total atomically wrt the timer IRQ writer. u64
    * isn't atomic on i386 and sched_account_ticks() bumps total
    * from IRQ context; a torn read would skew the snapshot by the
    * size of the carry. We're curr here, so disable_interrupts is
    * enough -- no other CPU exists.
    */
   disable_interrupts(&var);
   {
      fairness_start_ticks[idx] = get_curr_task()->ticks.total;
   }
   enable_interrupts(&var);

   /*
    * CPU-bound work: spin in a small empty loop, checking the
    * stop flag at the top of every iteration. The volatile counter
    * keeps the compiler from elaborating the loop away.
    */
   while (!atomic_load(&fairness_stop_flag)) {

      for (volatile int i = 0; i < 1024; i++) { }
   }

   disable_interrupts(&var);
   {
      fairness_end_ticks[idx] = get_curr_task()->ticks.total;
   }
   enable_interrupts(&var);
}

void selftest_fairness(void)
{
   int tids[FAIRNESS_THREADS];
   u64 ticks_used[FAIRNESS_THREADS];
   u64 total = 0;
   u64 min_used = (u64) -1;
   u64 max_used = 0;

   atomic_store(&fairness_stop_flag, 0);
   atomic_store(&fairness_ready_count, 0);

   for (int i = 0; i < FAIRNESS_THREADS; i++) {

      fairness_start_ticks[i] = 0;
      fairness_end_ticks[i] = 0;
      tids[i] = kthread_create(fairness_thread, 0, (void *)(ulong)i);
      VERIFY(tids[i] > 0);
   }

   /* Wait for all participants to clear the barrier before timing. */
   while (atomic_load(&fairness_ready_count) < FAIRNESS_THREADS)
      kernel_yield();

   /* Measurement window. */
   kernel_sleep(FAIRNESS_DURATION_TICKS);

   /* Tell them to exit; threads snapshot end_ticks before returning. */
   atomic_store(&fairness_stop_flag, 1);
   kthread_join_all(tids, FAIRNESS_THREADS, true);

   /*
    * All snapshots are now committed and the workers are gone.
    * Compute per-thread "ticks spent in the work loop" and the
    * statistics across them.
    */
   for (int i = 0; i < FAIRNESS_THREADS; i++) {

      ticks_used[i] = fairness_end_ticks[i] - fairness_start_ticks[i];
      total += ticks_used[i];

      if (ticks_used[i] < min_used)
         min_used = ticks_used[i];

      if (ticks_used[i] > max_used)
         max_used = ticks_used[i];
   }

   const u64 avg = total / FAIRNESS_THREADS;
   const u64 spread = max_used - min_used;
   const u64 spread_pct = avg ? (spread * 100) / avg : 0;

   for (int i = 0; i < FAIRNESS_THREADS; i++) {

      const u64 c = ticks_used[i];
      const u64 dev = c > avg ? c - avg : avg - c;
      const u64 dev_pct = avg ? (dev * 100) / avg : 0;

      printk("fairness: tid=%d ticks=%llu (dev=%llu%%)\n",
             tids[i],
             (unsigned long long) c,
             (unsigned long long) dev_pct);
   }

   printk("fairness: avg=%llu spread=%llu (%llu%%)\n",
          (unsigned long long) avg,
          (unsigned long long) spread,
          (unsigned long long) spread_pct);

   VERIFY(spread_pct <= FAIRNESS_TOLERANCE_PCT);
   se_regular_end();
}

REGISTER_SELF_TEST(fairness, se_med, &selftest_fairness)
