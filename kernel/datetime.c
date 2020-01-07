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
#include <tilck/kernel/process.h>

#define FULL_RESYNC_MAX_ATTEMPTS       10

const char *weekdays[7] =
{
   "Sunday",
   "Monday",
   "Tuesday",
   "Wednesday",
   "Thursday",
   "Friday",
   "Saturday",
};

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

// Regular value
u32 clock_drift_adj_loop_delay = 600 * TIMER_HZ;

// Value suitable for the `time` selftest
// u32 clock_drift_adj_loop_delay = 60 * TIMER_HZ;

extern u64 __time_ns;
extern u32 __tick_duration;
extern int __tick_adj_val;
extern int __tick_adj_ticks_rem;

bool clock_in_full_resync(void)
{
   return in_full_resync;
}

static int clock_get_second_drift2(bool enable_preempt_on_exit)
{
   struct datetime d;
   s64 sys_ts, hw_ts;
   u32 under_sec;
   u64 ts;

   while (true) {

      disable_preemption();

      hw_read_clock(&d);
      ts = get_sys_time();
      under_sec = (u32)(ts % TS_SCALE);

      /*
       * We don't want to measure the drift when we're too close to the second
       * border line, because there's a real chance to measure this way a
       * non-existent clock drift. For example: suppose that the seconds value
       * of the real clock time is 34.999, but we read just 34, of course.
       * If now our system time is ahead by even just 1 ms [keep in mind we
       * don't disable the interrupts and ticks to continue to increase], we'd
       * read something like 35.0001 and get 35 after the truncation. Therefore,
       * we'll "measure" +1 second of drift, which is completely false! It makes
       * only sense to measure the drift in the middle of the second.
       */
      if (IN_RANGE(under_sec, TS_SCALE/4, TS_SCALE/4*3)) {

         sys_ts = boot_timestamp + (s64)(ts / TS_SCALE);

         if (enable_preempt_on_exit)
            enable_preemption();

         break;
      }

      /* We weren't in the middle of the second. Sleep 0.1s and try again */
      enable_preemption();
      kernel_sleep(TIMER_HZ / 10);
   }

   hw_ts = datetime_to_timestamp(d);
   return (int)(sys_ts - hw_ts);
}

int clock_get_second_drift(void)
{
   return clock_get_second_drift2(true);
}

void clock_drift_adj()
{
   struct datetime d;
   s64 hw_ts, ts;
   u64 hw_time_ns;
   int adj_val, adj_ticks;
   int drift, abs_drift, adj_cnt;
   u32 attempts_cnt, full_resync_failed_attempts;
   uptr var;

   /* Sleep 1 second after boot, in order to get a real value of `__time_ns` */
   kernel_sleep(TIMER_HZ);
   abs_drift = 0;
   full_resync_failed_attempts = 0;

   /*
    * When Tilck starts, in init_system_time() we register system clock's time.
    * But that time has a resolution of one second. After that, we keep the
    * time using PIT's interrupts and here below we compensate any drifts.
    *
    * The problem is that, since init_system_time() it's super easy for us to
    * hit a clock drift because `boot_timestamp` is in seconds. For example, we
    * had no way to know that we were in second 23.99: we'll see just second 23
    * and start counting from there. We inevitably start with a drift < 1 sec.
    *
    * Now, we could in theory avoid that by looping in init_system_time() until
    * time changes, but that would mean wasting up to 1 sec of boot time. That's
    * completely unacceptable. What we can do instead, is to boot and start
    * working knowing that we have a clock drift < 1 sec and then, in this
    * kernel thread do the loop, waiting for the time to change and calculating
    * this way, the initial clock drift.
    */

full_resync:
   in_full_resync = true;
   attempts_cnt = 0;
   disable_preemption();
   hw_read_clock(&d);
   hw_ts = datetime_to_timestamp(d);

   while (true) {

      hw_read_clock(&d);
      ts = datetime_to_timestamp(d);
      attempts_cnt++;

      if (ts != hw_ts) {

         /*
          * BOOM! We just detected the exact moment when the HW clock changed
          * the timestamp (seconds). Now, we have to be super quick about
          * calculating the adjustments.
          *
          * NOTE: we're leaving the loop with preemption disabled!
          */
         break;
      }

      /*
       * From time to time we _have to_ allow other tasks to get some job done,
       * not stealing the CPU for a whole full second.
       */
      if (!(attempts_cnt % 300)) {

         enable_preemption();
         {
            /* Sleep for a 1/5 of a second */
            kernel_sleep(TIMER_HZ / 5);
         }
         disable_preemption();

         /*
          * Now that we're back, we have to re-read the "old" clock value
          * because time passed and it's very likely that we're in a new second.
          * Without re-reading this "old" value, on the next iteration we might
          * hit the condition `ts != hw_ts` thinking that we've found the second
          * edge, while just too much time passed.
          *
          * Therefore, re-reading the old value (hw_ts) fully restarts our
          * search for the bleeding edge of the second, hoping that in the next
          * burst of attempts we'll be lucky enough to find the exact moment
          * when the HW clock changes the second.
          *
          * NOTE: this code has been tested with an infinite loop in `init`
          * stealing competing for the CPU with this kernel thread and,
          * reliably, in a few seconds we had been able to end this loop.
          */
         hw_read_clock(&d);
         hw_ts = datetime_to_timestamp(d);
      }
   }

   /*
    * Now that we waiting until the seconds changed, we have to very quickly
    * calculate our initial drift (offset) and set __tick_adj_val and
    * __tick_adj_ticks_rem accordingly to compensate it.
    */

   disable_interrupts(&var);
   {
      hw_time_ns = round_up_at64(__time_ns, TS_SCALE);

      if (hw_time_ns > __time_ns) {

         STATIC_ASSERT(TS_SCALE <= BILLION);

         /* NOTE: abs_drift cannot be > TS_SCALE [typically, 1 BILLION] */
         abs_drift = (int)(hw_time_ns - __time_ns);
         __tick_adj_val = (TS_SCALE / TIMER_HZ) / 10;
         __tick_adj_ticks_rem = abs_drift / __tick_adj_val;
      }
   }
   enable_interrupts(&var);

   /*
    * We know that we need at most 10 seconds to compensate 1 second of drift,
    * which is the max we can get at boot-time. Now, just to be sure, wait 20s
    * and then check we have absolutely no drift measurable in seconds.
    */
   enable_preemption();
   kernel_sleep(15 * TIMER_HZ);
   drift = clock_get_second_drift2(true);

   if (drift) {

      if (++full_resync_failed_attempts > FULL_RESYNC_MAX_ATTEMPTS)
         panic("Time-management: drift(%d) must be zero after sync", drift);
   }

   /*
    * Everything is alright. Sleep some time and then start the actual infinite
    * loop of this thread, which will compensate any clock drifts that might
    * occur as Tilck runs for a long time.
    */
   in_full_resync = false;
   kernel_sleep(clock_drift_adj_loop_delay);
   adj_cnt = 0;
   full_resync_failed_attempts = 0;

   while (true) {

      /* NOTE: this disables the preemption */
      drift = clock_get_second_drift2(false);

      if (drift) {

         if (++adj_cnt > 6) {

            /*
             * The periodic drift compensation works great even in the
             * "long run" but it's expected very slowly to accumulate with
             * time some sub-second drift that cannot be measured directly,
             * because of HW clock's 1s resolution. We'll inevitably end up
             * introducing some error while compensating the "apprently" 1s
             * drift (which, in reality was 1.01s, for example).
             *
             * To compensate even this 2nd-order problem, it's worth from time
             * to time to do a full-resync. This should happen less then once
             * every 24 h, depending on how precise the PIT is.
             */
            goto full_resync;
         }

         abs_drift = (drift > 0 ? drift : -drift);
         adj_val = (TS_SCALE / TIMER_HZ) / (drift > 0 ? -10 : 10);
         adj_ticks = abs_drift * TIMER_HZ * 10;

         disable_interrupts(&var);
         {
            __tick_adj_val = adj_val;
            __tick_adj_ticks_rem = adj_ticks;
         }
         enable_interrupts(&var);
      }

      enable_preemption();
      kernel_sleep(clock_drift_adj_loop_delay);
   }
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
   uptr var;
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

static void real_time_get_timespec(struct timespec *tp)
{
   const u64 t = get_sys_time();

   tp->tv_sec = (time_t)boot_timestamp + (time_t)(t / TS_SCALE);

   if (TS_SCALE <= BILLION)
      tp->tv_nsec = (t % TS_SCALE) * (BILLION / TS_SCALE);
   else
      tp->tv_nsec = (t % TS_SCALE) / (TS_SCALE / BILLION);
}

static void monotonic_time_get_timespec(struct timespec *tp)
{
   /* Same as the real_time clock, for the moment */
   real_time_get_timespec(tp);
}

static void task_cpu_get_timespec(struct timespec *tp)
{
   struct task *ti = get_curr_task();

   disable_preemption();
   {
      const u64 tot = ti->total_ticks * __tick_duration;

      tp->tv_sec = (time_t)(tot / TS_SCALE);

      if (TS_SCALE <= BILLION)
         tp->tv_nsec = (tot % TS_SCALE) * (BILLION / TS_SCALE);
      else
         tp->tv_nsec = (tot % TS_SCALE) / (TS_SCALE / BILLION);
   }
   enable_preemption();
}

int sys_gettimeofday(struct timeval *user_tv, struct timezone *user_tz)
{
   struct timeval tv;
   struct timespec tp;
   struct timezone tz = {
      .tz_minuteswest = 0,
      .tz_dsttime = 0,
   };

   real_time_get_timespec(&tp);

   tv = (struct timeval) {
      .tv_sec = tp.tv_sec,
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

int sys_clock_gettime(clockid_t clk_id, struct timespec *user_tp)
{
   struct timespec tp;

   if (!user_tp)
      return -EINVAL;

   switch (clk_id) {

      case CLOCK_REALTIME:
      case CLOCK_REALTIME_COARSE:
         real_time_get_timespec(&tp);
         break;

      case CLOCK_MONOTONIC:
      case CLOCK_MONOTONIC_COARSE:
      case CLOCK_MONOTONIC_RAW:
         monotonic_time_get_timespec(&tp);
         break;

      case CLOCK_PROCESS_CPUTIME_ID:
      case CLOCK_THREAD_CPUTIME_ID:
         task_cpu_get_timespec(&tp);
         break;

      default:
         printk("WARNING: unsupported clk_id: %d\n", clk_id);
         return -EINVAL;
   }

   if (copy_to_user(user_tp, &tp, sizeof(tp)) < 0)
      return -EFAULT;

   return 0;
}

int sys_clock_getres(clockid_t clk_id, struct timespec *user_res)
{
   struct timespec tp;

   switch (clk_id) {

      case CLOCK_REALTIME:
      case CLOCK_REALTIME_COARSE:
      case CLOCK_MONOTONIC:
      case CLOCK_MONOTONIC_COARSE:
      case CLOCK_MONOTONIC_RAW:
      case CLOCK_PROCESS_CPUTIME_ID:
      case CLOCK_THREAD_CPUTIME_ID:

         tp = (struct timespec) {
            .tv_sec = 0,
            .tv_nsec = BILLION/TIMER_HZ,
         };

         break;

      default:
         printk("WARNING: unsupported clk_id: %d\n", clk_id);
         return -EINVAL;
   }

   if (copy_to_user(user_res, &tp, sizeof(tp)) < 0)
      return -EFAULT;

   return 0;
}
