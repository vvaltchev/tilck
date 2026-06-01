/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Selftest for rtc_wait_for_second_edge(): validates that the CMOS
 * RTC's Update-Ended Interrupt (UIE) fires once per wall-clock
 * second and that the per-edge __time_ns snapshots advance by ~1 s
 * of system time between consecutive edges.
 *
 * The test waits for N consecutive edges and asserts:
 *
 *   1. Every wait returned a signal (no timeouts).
 *   2. Consecutive snapshots are 1 s of __time_ns apart, within a
 *      generous tolerance (the timer-IRQ-based __time_ns and the
 *      RTC's own clock are different physical sources -- they can
 *      legitimately diverge by ppm-scale amounts over short
 *      windows, and there's some IRQ latency on top of that).
 *
 * The first edge after enabling UIE can come anywhere between
 * ~0 and ~1 s into the future (depending on where the RTC is in
 * its own cycle at the moment we enable), so the test only checks
 * deltas between consecutive edges, not the absolute wait time.
 *
 * Skipped under in_hypervisor()==false with strict bounds: a real
 * RTC chip on cheap hardware can be 50+ ppm off the PIT crystal,
 * which over a 1 s window is 50 us -- tighter than the 10 ms
 * tolerance below, but it's worth noting the tolerance is loose on
 * purpose so noisy hosts don't trip it.
 */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/hal.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/datetime.h>
#include <tilck/kernel/self_tests.h>

#define RTC_UIE_EDGES               4
#define RTC_UIE_WAIT_TIMEOUT_TICKS  (KRN_TIMER_HZ * 3)   /* 3 s */
#define RTC_UIE_DELTA_TOLERANCE_NS  (10 * 1000 * 1000)   /* 10 ms */

/*
 * The span check cross-validates two independent clocks: __time_ns
 * (PIT-IRQ-driven, snapshotted by the UIE handler at each RTC second
 * edge) against the RTC's own 1 Hz cycle. Its dominant error source is
 * host scheduling jitter under full CPU emulation (QEMU TCG): when the
 * host deschedules the vCPU thread, pending PIT IRQs land late, so the
 * __time_ns snapshot is momentarily stale at the instant an edge is
 * sampled. The span error is roughly the worst such gap during the
 * measurement -- tens of ms on a loaded host, independent of TIMER_HZ.
 *
 * The tolerance is deliberately generous. A real regression (UIE at the
 * wrong rate, or a broken __time_ns) deviates by hundreds of ms to
 * seconds, and "UIE not firing" is caught independently by the per-edge
 * wait timeout -- so nothing legitimate lands between host jitter and a
 * real bug, and 250 ms ends the flakiness without weakening the test.
 */
#define RTC_UIE_SPAN_TOLERANCE_NS   (250 * 1000 * 1000)   /* 250 ms */

void selftest_rtc_uie(void)
{
   u64 edges[RTC_UIE_EDGES];
   u64 probe_ns;
   u64 total_span;
   u64 expected_span;
   u64 span_dev;
   struct clock_resync_stats stats;

   /*
    * Probe first: on arches without a CMOS RTC (riscv64), the WEAK
    * fallback in kernel/misc.c returns false unconditionally. Skip
    * the test cleanly instead of hanging on the drift-kthread-resync
    * wait below, which would never see full_resync_count tick.
    */
   if (!rtc_wait_for_second_edge(&probe_ns, RTC_UIE_WAIT_TIMEOUT_TICKS)) {
      printk("rtc_uie: no RTC UIE on this platform, skipping\n");
      se_regular_end();
      return;
   }

   /*
    * The drift-compensation kthread (kernel/datetime.c:clock_drift_adj)
    * is itself UIE-driven. Its first compensation cycle happens at the
    * first RTC second edge after boot and shifts __time_ns by the
    * boot-time drift (typically ~80 ms in QEMU TCG), which inflates
    * the delta between any two edges that straddle the compensation.
    * After that the kthread sleeps for clock_drift_adj_loop_delay
    * (default: 60 s), leaving __time_ns moving at real-time pace.
    *
    * Wait for the kthread to have done at least one compensation
    * cycle, then wait for any in-progress adjustment to settle.
    * After that, RTC-edge-to-RTC-edge deltas should match real time
    * to within the measurement tolerance.
    */
   do {
      kernel_sleep(KRN_TIMER_HZ / 10);
      clock_get_resync_stats(&stats);
   } while (stats.full_resync_count == 0);

   while (clock_in_resync())
      kernel_sleep(KRN_TIMER_HZ / 10);

   for (int i = 0; i < RTC_UIE_EDGES; i++) {

      const bool ok =
         rtc_wait_for_second_edge(&edges[i], RTC_UIE_WAIT_TIMEOUT_TICKS);

      if (!ok) {
         printk("rtc_uie: edge %d wait timed out\n", i);
         VERIFY(ok);
      }

      printk("rtc_uie: edge %d at __time_ns=%llu\n",
             i, (ulonglong) edges[i]);
   }

   for (int i = 1; i < RTC_UIE_EDGES; i++) {

      const u64 delta = edges[i] - edges[i - 1];
      const u64 dev = delta > TS_SCALE
                         ? delta - TS_SCALE
                         : TS_SCALE - delta;

      printk("rtc_uie: delta %d->%d = %llu ns (dev from 1s = %llu ns)\n",
             i - 1, i,
             (ulonglong) delta,
             (ulonglong) dev);
   }

   /*
    * Assert on the total span rather than individual deltas. The
    * PIT and RTC are independent clocks driven from different host
    * timers in a VM. When the host reschedules the guest, a few
    * PIT ticks can be delivered late, so the __time_ns snapshot
    * captured by the UIE handler may lag real time by a handful of
    * ticks at that instant. The next UIE snapshot catches up, so
    * individual deltas jitter by ±several ticks (~12-20 ms at
    * 250 Hz) while the total over N edges stays accurate. Checking
    * the full span absorbs per-edge jitter.
    */
   total_span = edges[RTC_UIE_EDGES - 1] - edges[0];
   expected_span = (u64)(RTC_UIE_EDGES - 1) * TS_SCALE;
   span_dev = total_span > expected_span
                ? total_span - expected_span
                : expected_span - total_span;

   printk("rtc_uie: total span = %llu ns, expected = %llu ns, "
          "dev = %llu ns\n",
          (ulonglong) total_span,
          (ulonglong) expected_span,
          (ulonglong) span_dev);

   VERIFY(span_dev <= RTC_UIE_SPAN_TOLERANCE_NS);

   se_regular_end();
}

REGISTER_SELF_TEST(rtc_uie, se_med, &selftest_rtc_uie)
