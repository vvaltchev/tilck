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
 *
 * What this test does NOT measure, and assumptions you should know
 * about before believing the result:
 *
 *   1. The metric is "scheduler ticks (PIT IRQs) attributed to a
 *      given thread", NOT wall-clock CPU time. ticks.total is
 *      incremented by sched_account_ticks() when the PIT IRQ fires
 *      while a thread is `curr`. kernel_sleep() is also tick-
 *      driven. Both consume the same guest virtual time clock --
 *      the host's wall clock is irrelevant.
 *
 *   2. The test is robust to host-side VCPU jitter on Tilck's
 *      current QEMU invocation. The 8254 PIT (kernel/arch/
 *      generic_x86/pit.c) advances on guest virtual time, and
 *      QEMU_COMMON_OPTS is just `-rtc base=localtime` -- no
 *      `clock=host`, no kvm-clock host-time tracking. If the host
 *      deschedules qemu-system-i386 for 100 ms, PIT IRQs simply
 *      don't fire during the gap: no missed-tick coalescing, no
 *      asymmetric catch-up on resume. Wall-clock duration of the
 *      test dilates; per-thread tick count does NOT.
 *
 *      If anyone ever overrides the clock to host-time tracking
 *      (`-rtc clock=host`, hpet with host-tick-policy=catchup,
 *      etc.), missed ticks would coalesce on VCPU resume into a
 *      single rapid burst aimed at whichever thread happened to
 *      be `curr` at the moment of resume. That's an asymmetric
 *      skew the 10% tolerance below would not absorb under heavy
 *      starvation. This test is one of the first things to break
 *      in that scenario; investigate the clock config before
 *      blaming the scheduler.
 *
 *   3. Workers (kbd, clock_drift_adj, the init worker, ...) and
 *      other kernel threads consume ticks during the measurement
 *      window. They steal ticks symmetrically from the 5 test
 *      threads on average -- shift the absolute counts down,
 *      don't widen the spread. Empirically negligible.
 *
 *   4. Barrier release and the start-ticks snapshot are sequential
 *      across threads (each thread takes its snapshot only when it
 *      is itself `curr` after the ready-count check passes), so
 *      different threads have different `start_ticks` reference
 *      points in absolute terms. The MEASUREMENT, however, is the
 *      per-thread (end - start) delta -- every thread's window is
 *      its own, so a thread that crossed the barrier 5 ticks
 *      after another doesn't get penalized in its delta.
 *
 *   5. The KRN_MINIMAL_TIME_SLICE=1 stress build pins
 *      SCHED_LATENCY_TICKS = MIN_GRANULARITY_TICKS = 1, so every
 *      timer tick is a preemption point. Round-robin is perfect
 *      in principle (and in practice, empirically: spread under
 *      that config measures ~1%), but the per-tick scheduler
 *      overhead is much higher and any housekeeping IRQ that
 *      lands asymmetrically on test threads becomes more visible.
 *      FAIRNESS_TOLERANCE_PCT is conditionally loosened to 25%
 *      for that build so the noise floor doesn't trip a
 *      regression that isn't there.
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
 * KRN_MINIMAL_TIME_SLICE pins SCHED_LATENCY/MIN_GRANULARITY to 1
 * tick (caveat #5 in the file header). Round-robin should still be
 * tight in principle, but the per-tick housekeeping overhead is
 * much higher and asymmetric IRQ landing on test threads becomes a
 * larger fraction of each thread's small sample. Loosen the
 * tolerance to 25% in that build -- still strict enough that a
 * scheduler regression which de-fairs the distribution would trip
 * it, but not so tight that the stress build's noise floor does.
 */
#if KRN_MINIMAL_TIME_SLICE
   #define FAIRNESS_TOLERANCE_PCT   25
#else
   #define FAIRNESS_TOLERANCE_PCT   10
#endif

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
      tids[i] = kthread_create(fairness_thread, 0, TO_PTR(i));
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
