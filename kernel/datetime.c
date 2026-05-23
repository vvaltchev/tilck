/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/datetime.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/sys_types.h>
#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/sched.h>

#include <tilck/mods/tracing.h>
#include <linux/time_compat.h>

const char *months3[12] =
{
   "Jan",
   "Feb",
   "Mar",
   "Apr",
   "May",
   "Jun",
   "Jul",
   "Aug",
   "Sep",
   "Oct",
   "Nov",
   "Dec",
};

static s64 boot_timestamp;
static bool in_full_resync;
static u64 last_resync_time_ns;        /* __time_ns at last drift measurement */
static s64 last_drift_ns;              /* signed ns drift at last measurement */

/* lifetime statistics about re-syncs */
static struct clock_resync_stats clock_rstats;

u32 clock_drift_adj_loop_delay = 60 * KRN_TIMER_HZ;

extern u64 __time_ns;
extern u32 __tick_duration;
extern int __tick_adj_val;
extern int __tick_adj_ticks_rem;

bool clock_in_full_resync(void)
{
   return in_full_resync;
}

bool clock_in_resync(void)
{
   ulong var;
   int rem;

   if (in_full_resync)
      return true;

   disable_interrupts(&var);
   {
      rem = __tick_adj_ticks_rem;
   }
   enable_interrupts(&var);
   return rem != 0;
}

void clock_get_resync_stats(struct clock_resync_stats *s)
{
   *s = clock_rstats;
}

/*
 * Drift compensation kthread.
 *
 * The system tracks wall-clock time as boot_timestamp + __time_ns/TS_SCALE.
 * boot_timestamp comes from the RTC at boot with 1-second resolution, so
 * our wall-clock is up to ~1 second off from real time at boot. Beyond
 * that, the PIT crystal isn't exactly synced to the RTC crystal (or to
 * host time inside a VM), so __time_ns drifts a few ppm relative to
 * true wall-clock over a day.
 *
 * The kthread closes both gaps:
 *
 *   1. On the first iteration, it learns __time_ns at the precise moment
 *      of an RTC second-edge (via UIE -- see rtc_wait_for_second_edge),
 *      computes how off-boot we were within that second, and applies a
 *      one-shot compensation. This subsumes the old "1-sec uncertainty
 *      at boot" without blocking boot.
 *
 *   2. On every subsequent iteration, it re-measures and applies a
 *      smaller correction. If the drift sign matches the previous
 *      iteration, the PIT's per-tick __tick_duration itself is nudged:
 *      that's a systemic problem (PIT crystal slightly faster or slower
 *      than RTC), and shrinking the per-tick value at the source
 *      converges faster than re-applying the same one-shot every minute.
 *
 * Compensation works through __tick_adj_val + __tick_adj_ticks_rem in
 * the timer IRQ handler: while ticks_rem > 0, each tick adds adj_val to
 * ns_delta (positive to speed __time_ns up, negative to slow it down).
 * Once the accumulated adjustment matches the measured drift, ticks_rem
 * hits zero and __time_ns advances at the regular tick_duration again.
 *
 * On arches without UIE support (riscv64 today's WEAK stub),
 * rtc_wait_for_second_edge returns false on the first call and this
 * kthread exits -- no alignment, but no harm either; boards without an
 * RTC simply have no way to compensate.
 */

static void clock_apply_drift(s64 drift_ns)
{
   /*
    * drift_ns > 0: __time_ns is AHEAD of wall-clock -> slow down.
    * drift_ns < 0: __time_ns is BEHIND wall-clock   -> speed up.
    *
    * Cap |adj_val| at 10% of nominal tick duration so __time_ns
    * doesn't visibly stutter; a 1-second drift converges in ~10
    * seconds at HZ=250.
    */
   ulong var;

   if (drift_ns == 0)
      return;

   const s32 max_step = (s32)__tick_duration / 10;
   const u64 abs_drift = drift_ns > 0 ? (u64)drift_ns : (u64)(-drift_ns);
   const s32 adj_val = drift_ns > 0 ? -max_step : max_step;
   const u32 adj_ticks = (u32)(abs_drift / (u32)max_step);

   disable_interrupts(&var);
   {
      __tick_adj_val = adj_val;
      __tick_adj_ticks_rem = (int)adj_ticks;
   }
   enable_interrupts(&var);
}

static void clock_nudge_tick_duration(s64 drift_ns)
{
   /*
    * Systemic-drift heuristic: if the current and previous drift
    * have the same sign, the PIT crystal is consistently off the
    * RTC, so __tick_duration itself is biased. Move it 75% toward
    * the implied correct value -- enough to converge fast, not so
    * much that we over-correct on the next iteration.
    */
   ulong var;

   if (!last_resync_time_ns)
      return;     /* first iteration -- nothing to compare against */

   /*
    * Use the full ns drift, not its whole-seconds truncation: a sub-
    * second `last_drift_value` got cast to 0, and `(x > 0) != (0 > 0)`
    * silently turned the sign check into "only nudge when drift_ns is
    * negative". That bias inflated __tick_duration over time in any
    * environment where a single bad measurement crossed zero.
    */
   if ((drift_ns > 0) != (last_drift_ns > 0))
      return;     /* sign flipped -- not systemic */

   const u64 time_gap_ns = __time_ns - last_resync_time_ns;
   const u64 ticks_gap = time_gap_ns / __tick_duration;
   if (ticks_gap == 0)
      return;

   const s64 drift_per_tick = drift_ns / (s64)ticks_gap;
   const s64 adjustment = drift_per_tick * 3 / 4;

   disable_interrupts(&var);
   {
      __tick_duration = (u32)((s64)__tick_duration - adjustment);
   }
   enable_interrupts(&var);
}

static void clock_drift_adj(void *unused)
{
   struct datetime d;
   u64 edge_ns;
   s64 hw_ts;
   s64 expected_ns;
   s64 drift_ns;

   while (true) {

      if (!rtc_wait_for_second_edge(&edge_ns, KRN_TIMER_HZ * 3))
         return;     /* No UIE on this platform -- give up. */

      hw_read_clock(&d);
      hw_ts = datetime_to_timestamp(d);
      expected_ns = (hw_ts - boot_timestamp) * (s64)TS_SCALE;
      drift_ns = (s64)edge_ns - expected_ns;

      in_full_resync = (last_resync_time_ns == 0);

      clock_apply_drift(drift_ns);
      clock_nudge_tick_duration(drift_ns);

      last_resync_time_ns = __time_ns;
      last_drift_ns = drift_ns;

      clock_rstats.full_resync_count++;
      if (drift_ns > (s64)TS_SCALE || drift_ns < -(s64)TS_SCALE)
         clock_rstats.multi_second_resync_count++;

      in_full_resync = false;

      trace_printk(5, "drift: %lld ns, adj_val=%d, adj_ticks=%d, "
                      "tick_dur=%u",
                   (long long) drift_ns,
                   __tick_adj_val,
                   __tick_adj_ticks_rem,
                   __tick_duration);

      kernel_sleep(clock_drift_adj_loop_delay);
   }
}

int clock_get_second_drift(void)
{
   struct datetime d;
   s64 sys_ts, hw_ts;

   disable_preemption();
   {
      hw_read_clock(&d);
      sys_ts = boot_timestamp + (s64)(get_sys_time() / TS_SCALE);
      hw_ts = datetime_to_timestamp(d);
   }
   enable_preemption();

   return (int)(sys_ts - hw_ts);
}

void init_system_time(void)
{
   struct datetime d;

   if (kthread_create(&clock_drift_adj, 0, NULL) < 0)
      printk("WARNING: unable to create a kthread for clock_drift_adj()\n");

   hw_read_clock(&d);
   boot_timestamp = datetime_to_timestamp(d);

   if (boot_timestamp < 0)
      panic("Invalid boot-time UNIX timestamp: %d\n", boot_timestamp);

   __time_ns = 0;
}

u64 get_sys_time(void)
{
   u64 ts;
   ulong var;
   disable_interrupts(&var);
   {
      ts = __time_ns;
   }
   enable_interrupts(&var);
   return ts;
}

s64 get_timestamp(void)
{
   const u64 ts = get_sys_time();
   return boot_timestamp + (s64)(ts / TS_SCALE);
}

struct k_timeval
k_ts64_to_k_timeval(struct k_timespec64 ts)
{
   return (struct k_timeval) {
      .tv_sec = (long) ts.tv_sec,
      .tv_usec = ts.tv_nsec / 1000,
   };
}

void ticks_to_timespec(u64 ticks, struct k_timespec64 *tp)
{
   const u64 tot = ticks * __tick_duration;

   tp->tv_sec = (s64)(tot / TS_SCALE);

   if (TS_SCALE <= BILLION)
      tp->tv_nsec = (tot % TS_SCALE) * (BILLION / TS_SCALE);
   else
      tp->tv_nsec = (tot % TS_SCALE) / (TS_SCALE / BILLION);
}

u64 timespec_to_ticks(const struct k_timespec64 *tp)
{
   u64 ticks = 0;
   ticks += div_round_up64((u64)tp->tv_sec * TS_SCALE, __tick_duration);

   if (TS_SCALE <= BILLION) {

      ticks +=
         div_round_up64(
            (u64)tp->tv_nsec / (BILLION / TS_SCALE), __tick_duration
         );

   } else {

      ticks +=
         div_round_up64(
            (u64)tp->tv_nsec * (TS_SCALE / BILLION), __tick_duration
         );
   }

   return ticks;
}

void real_time_get_timespec(struct k_timespec64 *tp)
{
   const u64 t = get_sys_time();

   tp->tv_sec = (s64)boot_timestamp + (s64)(t / TS_SCALE);

   if (TS_SCALE <= BILLION)
      tp->tv_nsec = (t % TS_SCALE) * (BILLION / TS_SCALE);
   else
      tp->tv_nsec = (t % TS_SCALE) / (TS_SCALE / BILLION);
}

void monotonic_time_get_timespec(struct k_timespec64 *tp)
{
   /* Same as the real_time clock, for the moment */
   real_time_get_timespec(tp);
}

static void
task_cpu_get_timespec(struct k_timespec64 *tp)
{
   struct task *ti = get_curr_task();

   disable_preemption();
   {
      ticks_to_timespec(ti->ticks.total, tp);
   }
   enable_preemption();
}

int sys_gettimeofday(struct k_timeval *user_tv, struct timezone *user_tz)
{
   struct k_timeval tv;
   struct k_timespec64 tp;

   struct timezone tz = {
      .tz_minuteswest = 0,
      .tz_dsttime = 0,
   };

   real_time_get_timespec(&tp);

   tv = (struct k_timeval) {
      .tv_sec = (long)tp.tv_sec,
      .tv_usec = tp.tv_nsec / 1000,
   };

   if (user_tv)
      if (copy_to_user(user_tv, &tv, sizeof(tv)) < 0)
         return -EFAULT;

   if (user_tz)
      if (copy_to_user(user_tz, &tz, sizeof(tz)) < 0)
         return -EFAULT;

   return 0;
}

int
do_clock_gettime(clockid_t clk_id, struct k_timespec64 *tp)
{
   switch (clk_id) {

      case CLOCK_REALTIME:
#ifdef CLOCK_REALTIME_COARSE
      case CLOCK_REALTIME_COARSE:
#endif
         real_time_get_timespec(tp);
         break;

      case CLOCK_MONOTONIC:
#ifdef CLOCK_MONOTONIC_COARSE
      case CLOCK_MONOTONIC_COARSE:
#endif
      case CLOCK_MONOTONIC_RAW:
         monotonic_time_get_timespec(tp);
         break;

      case CLOCK_PROCESS_CPUTIME_ID:
      case CLOCK_THREAD_CPUTIME_ID:
         task_cpu_get_timespec(tp);
         break;

      default:
         printk("WARNING: unsupported clk_id: %d\n", clk_id);
         return -EINVAL;
   }

   return 0;
}

int
do_clock_getres(clockid_t clk_id, struct k_timespec64 *res)
{
   switch (clk_id) {

      case CLOCK_REALTIME:
#ifdef CLOCK_REALTIME_COARSE
      case CLOCK_REALTIME_COARSE:
#endif
      case CLOCK_MONOTONIC:
#ifdef CLOCK_MONOTONIC_COARSE
      case CLOCK_MONOTONIC_COARSE:
#endif
      case CLOCK_MONOTONIC_RAW:
      case CLOCK_PROCESS_CPUTIME_ID:
      case CLOCK_THREAD_CPUTIME_ID:

         *res = (struct k_timespec64) {
            .tv_sec = 0,
            .tv_nsec = BILLION/KRN_TIMER_HZ,
         };

         break;

      default:
         printk("WARNING: unsupported clk_id: %d\n", clk_id);
         return -EINVAL;
   }

   return 0;
}

/*
 * ----------------- SYSCALLS -----------------------
 */

int sys_clock_gettime32(clockid_t clk_id, struct k_timespec32 *user_tp)
{
   struct k_timespec64 tp64;
   struct k_timespec32 tp32;
   int rc;

   if (!user_tp)
      return -EINVAL;

   if ((rc = do_clock_gettime(clk_id, &tp64)))
      return rc;

   tp32 = (struct k_timespec32) {
      .tv_sec = (s32) tp64.tv_sec,
      .tv_nsec = tp64.tv_nsec,
   };

   if (copy_to_user(user_tp, &tp32, sizeof(tp32)) < 0)
      return -EFAULT;

   return 0;
}

int sys_clock_gettime(clockid_t clk_id, struct k_timespec64 *user_tp)
{
   struct k_timespec64 tp;
   int rc;

   if (!user_tp)
      return -EINVAL;

   if ((rc = do_clock_gettime(clk_id, &tp)))
      return rc;

   if (copy_to_user(user_tp, &tp, sizeof(tp)) < 0)
      return -EFAULT;

   return 0;
}

int sys_clock_getres_time32(clockid_t clk_id, struct k_timespec32 *user_res)
{
   struct k_timespec64 tp64;
   struct k_timespec32 tp32;
   int rc;

   if (!user_res)
      return -EINVAL;

   if ((rc = do_clock_getres(clk_id, &tp64)))
      return rc;

   tp32 = (struct k_timespec32) {
      .tv_sec = (s32) tp64.tv_sec,
      .tv_nsec = tp64.tv_nsec,
   };

   if (copy_to_user(user_res, &tp32, sizeof(tp32)) < 0)
      return -EFAULT;

   return 0;
}

int sys_clock_getres(clockid_t clk_id, struct k_timespec64 *user_res)
{
   struct k_timespec64 tp;
   int rc;

   if (!user_res)
      return -EINVAL;

   if ((rc = do_clock_gettime(clk_id, &tp)))
      return rc;

   if (copy_to_user(user_res, &tp, sizeof(tp)) < 0)
      return -EFAULT;

   return 0;
}
