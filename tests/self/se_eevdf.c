/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * EEVDF smoke test on the real kernel.
 *
 * Distinct from selftest_fairness: a fast, narrowly-scoped tripwire
 * that exercises the EEVDF code path under equal-slice + equal-
 * weight (the only configuration Tilck supports today) and checks
 * a few structural properties at the end. If this fails alone (with
 * selftest_fairness still passing), suspect a localized EEVDF
 * regression -- a stuck slice, a corrupted avg_vruntime, etc. If
 * both fail, the fairness regression is real.
 *
 * What's checked here that selftest_fairness doesn't check:
 *
 *   - per-thread `slice` lands within the dynamic-slice bounds
 *     [MIN_GRANULARITY_TICKS, SCHED_LATENCY_TICKS] * VRUNTIME_SCALE
 *     at the end of each thread's run. Catches a broken
 *     sched_compute_slice() / sched_start_quantum() pairing where
 *     the slice budget would be wildly off.
 *
 *   - per-thread `vruntime` advanced past zero, confirming the
 *     per-tick increment fires for the EEVDF code path (a no-op
 *     scheduler would still produce fair tick distributions if
 *     other machinery picked tasks correctly, but vruntime would
 *     stay at zero).
 *
 * The fairness spread check is loose (50% tolerance over a short
 * window): this is a smoke test, not a tightness test.
 */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/atomics.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/self_tests.h>

#define EEVDF_THREADS         3
#define EEVDF_DURATION_TICKS  (KRN_TIMER_HZ / 2)   /* 0.5 seconds */

/*
 * VRUNTIME_SCALE is sched.c-internal; mirror the value here for the
 * range check below. selftest_fairness uses a similar idiom (no
 * scheduler-internal headers from selftests).
 */
#define EEVDF_VRUNTIME_SCALE  16

static atomic_int_t eevdf_stop_flag;
static atomic_int_t eevdf_ready_count;
static u64 eevdf_total[EEVDF_THREADS];
static u32 eevdf_slice[EEVDF_THREADS];
static u64 eevdf_vruntime[EEVDF_THREADS];

static void eevdf_thread(void *arg)
{
   const int idx = (int)(ulong)arg;
   ulong var;

   /* Same barrier idiom as selftest_fairness: every thread must
    * have been scheduled at least once before any of them starts
    * counting, so the measurement window is symmetric. */
   atomic_fetch_add(&eevdf_ready_count, 1);
   while (atomic_load(&eevdf_ready_count) < EEVDF_THREADS)
      kernel_yield();

   while (!atomic_load(&eevdf_stop_flag)) {
      for (volatile int i = 0; i < 1024; i++) { }
   }

   /* Snapshot self-state at exit. Interrupt-disabled to keep u64
    * reads atomic w.r.t. the timer-IRQ writer on i386. */
   disable_interrupts(&var);
   {
      struct task *ti = get_curr_task();
      eevdf_total[idx] = ti->ticks.total;
      eevdf_slice[idx] = ti->ticks.slice;
      eevdf_vruntime[idx] = atomic_load(&ti->ticks.vruntime);
   }
   enable_interrupts(&var);
}

void selftest_eevdf(void)
{
   int tids[EEVDF_THREADS];
   u64 min_total = (u64) -1;
   u64 max_total = 0;
   const u32 min_slice =
      (u32) MIN_GRANULARITY_TICKS * EEVDF_VRUNTIME_SCALE;
   const u32 max_slice =
      (u32) SCHED_LATENCY_TICKS * EEVDF_VRUNTIME_SCALE;

   atomic_store(&eevdf_stop_flag, 0);
   atomic_store(&eevdf_ready_count, 0);

   for (int i = 0; i < EEVDF_THREADS; i++) {
      eevdf_total[i] = 0;
      eevdf_slice[i] = 0;
      eevdf_vruntime[i] = 0;
      tids[i] = kthread_create(eevdf_thread, 0, TO_PTR(i));
      VERIFY(tids[i] > 0);
   }

   while (atomic_load(&eevdf_ready_count) < EEVDF_THREADS)
      kernel_yield();

   kernel_sleep(EEVDF_DURATION_TICKS);

   atomic_store(&eevdf_stop_flag, 1);
   kthread_join_all(tids, EEVDF_THREADS, true);

   /*
    * Structural checks: per-thread state at the time of exit must
    * sit inside the EEVDF invariants. A failure here is a localized
    * EEVDF regression, not a fairness regression.
    */
   for (int i = 0; i < EEVDF_THREADS; i++) {

      printk("eevdf: tid=%d total=%llu slice=%u vruntime=%llu\n",
             tids[i],
             (unsigned long long) eevdf_total[i],
             eevdf_slice[i],
             (unsigned long long) eevdf_vruntime[i]);

      /* No thread should be starved. */
      VERIFY(eevdf_total[i] > 0);

      /* vruntime must have advanced (the per-tick increment fired). */
      VERIFY(eevdf_vruntime[i] > 0);

      /* slice must land within the dynamic-slice clamp. */
      VERIFY(eevdf_slice[i] >= min_slice);
      VERIFY(eevdf_slice[i] <= max_slice);
   }

   /*
    * Loose fairness sanity check: spread under 50%. This is a smoke
    * test -- the real fairness contract is selftest_fairness's job.
    */
   for (int i = 0; i < EEVDF_THREADS; i++) {

      if (eevdf_total[i] < min_total)
         min_total = eevdf_total[i];

      if (eevdf_total[i] > max_total)
         max_total = eevdf_total[i];
   }

   const u64 spread = max_total - min_total;
   const u64 spread_pct = min_total ? (spread * 100) / min_total : 0;

   printk("eevdf: spread=%llu (%llu%% over min)\n",
          (unsigned long long) spread,
          (unsigned long long) spread_pct);

   VERIFY(spread_pct <= 50);
   se_regular_end();
}

REGISTER_SELF_TEST(eevdf, se_short, &selftest_eevdf)
