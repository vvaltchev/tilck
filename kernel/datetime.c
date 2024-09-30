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

#define FULL_RESYNC_MAX_ATTEMPTS          10
#define MICRO_ATTEMPTS_BEFORE_SLEEP      400
#define MICRO_ATTEMPTS_TIMEOUT_SEC        60

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

#if KRN_CLOCK_DRIFT_COMP
static bool first_sssync_failed; /* first_sub_second_sync_failed */
static int adj_cnt;              /* adjustments count (temporary, gets reset) */
#endif

/* lifetime statistics about re-syncs */
static struct clock_resync_stats clock_rstats;

u32 clock_drift_adj_loop_delay = 60 * TIMER_HZ;

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

#if KRN_CLOCK_DRIFT_COMP

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
         break;
      }

      /* We weren't in the middle of the second. Sleep 0.1s and try again */
      enable_preemption_nosched();
      kernel_sleep(TIMER_HZ / 10);
   }

   /* NOTE: here we always have the preemption disabled */
   if (enable_preempt_on_exit)
      enable_preemption();

   hw_ts = datetime_to_timestamp(d);
   return (int)(sys_ts - hw_ts);
}

static u8 pseudo_random_vals_250[] =
{
   246, 138, 221, 194, 143, 158, 14, 0, 227, 193, 86, 6, 207, 89, 204, 227, 162,
   216, 244, 11, 150, 42, 119, 232, 237, 4, 204, 126, 152, 40, 180, 61, 162,
   249, 205, 109, 133, 170, 96, 154, 123, 35, 182, 69, 125, 218, 59, 219, 141,
   35, 47, 33, 23, 249, 114, 133, 42, 218, 243, 174, 147, 108, 172, 206, 116,
   23, 179, 96, 3, 241, 170, 233, 101, 87, 27, 76, 61, 21, 144, 199, 77, 81,
   247, 159, 114, 230, 44, 153, 114, 171, 78, 172, 26, 37, 109, 127, 78, 208,
   165, 234, 210, 23, 169, 207, 42, 178, 72, 99, 37, 5, 21, 119, 17, 58, 174,
   101, 45, 112, 219, 53, 112, 239, 244, 225, 126, 24, 163, 237, 234, 237, 37,
   250, 245, 111, 182, 65, 111, 227, 134, 117, 129, 31, 212, 176, 63, 137, 241,
   169, 138, 166, 60, 116, 20, 141, 92, 17, 67, 239, 245, 185, 178, 157, 176,
   25, 201, 123, 151, 24, 116, 199, 4, 84, 147, 225, 157, 224, 187, 211, 94,
   233, 133, 31, 2, 198, 92, 95, 249, 125, 14, 89, 105, 86, 243, 119, 243, 54,
   60, 57, 93, 234, 165, 71, 80, 226, 137, 32, 198, 121, 68, 185, 100, 154, 122,
   28, 201, 128, 3, 55, 78, 130, 248, 215, 33, 129, 203, 86, 221, 240, 57, 155,
   239, 112, 29, 2, 90, 92, 72, 104, 26, 205, 110, 66, 243, 189, 108, 182, 236,
   82, 123, 154, 156, 33, 127, 143, 46, 241
};
STATIC_ASSERT(sizeof(pseudo_random_vals_250) == 256);

static u8 pseudo_random_vals_50[] =
{
   36, 7, 38, 4, 12, 39, 36, 10, 41, 46, 6, 35, 16, 32, 45, 42, 33, 35, 29, 43,
   35, 31, 7, 30, 23, 22, 41, 36, 10, 26, 1, 45, 48, 5, 48, 29, 17, 36, 35, 40,
   14, 12, 3, 10, 6, 0, 22, 10, 4, 6, 35, 41, 28, 0, 0, 42, 1, 3, 47, 2, 27, 50,
   36, 48, 42, 20, 11, 22, 43, 6, 33, 1, 30, 45, 48, 40, 37, 12, 17, 46, 34, 46,
   9, 46, 15, 28, 37, 4, 47, 20, 23, 42, 45, 24, 41, 42, 14, 10, 46, 29, 29, 42,
   20, 13, 40, 35, 0, 21, 18, 49, 50, 25, 34, 2, 25, 4, 14, 14, 44, 47, 4, 40,
   44, 39, 17, 42, 15, 50, 25, 29, 7, 1, 13, 24, 40, 29, 32, 16, 47, 14, 4, 6,
   50, 17, 4, 44, 45, 49, 7, 17, 10, 13, 46, 35, 25, 0, 25, 37, 44, 34, 32, 40,
   15, 2, 41, 32, 39, 11, 24, 34, 4, 33, 10, 4, 29, 14, 24, 3, 17, 34, 48, 18,
   47, 15, 44, 31, 35, 18, 49, 26, 32, 47, 16, 33, 21, 8, 24, 30, 11, 34, 28,
   10, 13, 20, 41, 21, 43, 24, 8, 28, 10, 9, 43, 22, 19, 42, 36, 36, 20, 4, 21,
   45, 42, 37, 50, 28, 0, 46, 10, 18, 25, 0, 30, 32, 48, 47, 26, 39, 48, 11, 6,
   25, 21, 46, 34, 26, 11, 5, 38, 16, 36, 13, 0, 25, 37, 37
};
STATIC_ASSERT(sizeof(pseudo_random_vals_50) == 256);

static bool clock_sub_second_resync(void)
{
   /*
    * Use static variables so that we can continue to index cycle inside the
    * pseudo-random arrays of values, instead of restarting from the beginning
    * every time.
    */
   static u32 micro_attempts_periods = 0;
   static u32 micro_attempts_long_periods = 0;

   struct datetime d;
   s64 hw_ts, ts, initial_ts;
   u64 hw_time_ns;
   int drift, abs_drift;
   u32 micro_attempts_cnt;
   u32 local_full_resync_fails = 0;

   ASSERT(is_preemption_enabled());
   trace_printk(5, "Start sub-second resync");

   hw_read_clock(&d);
   initial_ts = datetime_to_timestamp(d);
   drift = clock_get_second_drift2(true);
   abs_drift = (drift > 0 ? drift : -drift);

   if (abs_drift > 1) {
      return false;
   }

retry:
   in_full_resync = true;
   micro_attempts_cnt = 0;
   disable_preemption();
   hw_read_clock(&d);
   hw_ts = datetime_to_timestamp(d);

   while (true) {

      hw_read_clock(&d);
      ts = datetime_to_timestamp(d);
      micro_attempts_cnt++;

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
      if (!(micro_attempts_cnt % MICRO_ATTEMPTS_BEFORE_SLEEP)) {

         enable_preemption_nosched();
         {
            u32 ticks;

            if ((ts - initial_ts) >= MICRO_ATTEMPTS_TIMEOUT_SEC) {
               trace_printk(5, "clock subsec sync taking too long, fail!");
               goto fail;
            }

            if (!(micro_attempts_periods % 15)) {

               ticks = pseudo_random_vals_250[
                  micro_attempts_long_periods %
                     ARRAY_SIZE(pseudo_random_vals_250)
               ];

               trace_printk(10, "clock drift: long period: %u",
                            micro_attempts_long_periods);

               micro_attempts_long_periods++;

            } else {

               ticks = pseudo_random_vals_50[

                  micro_attempts_periods %
                     ARRAY_SIZE(pseudo_random_vals_50)
               ];
            }

            /*printk("clock drift: sleep for %u ticks. Ts: %lld\n", ticks, ts);*/
            kernel_sleep(ticks);
            micro_attempts_periods++;
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

   trace_printk(5, "clock drift: found split sec moment!\n");

   /*
    * Now that we waited until the seconds changed, we have to very quickly
    * calculate our initial drift (offset) and set __tick_adj_val and
    * __tick_adj_ticks_rem accordingly to compensate it.
    */

   disable_interrupts_forced();
   {
      hw_time_ns = round_up_at64(__time_ns, TS_SCALE);

      if (hw_time_ns > __time_ns) {

         STATIC_ASSERT(TS_SCALE <= BILLION);

         /* NOTE: abs_drift cannot be > TS_SCALE [typically, 1 BILLION] */
         abs_drift = (int)(hw_time_ns - __time_ns);
         __tick_adj_val = (TS_SCALE / TIMER_HZ) / 10;
         __tick_adj_ticks_rem = abs_drift / __tick_adj_val;

      } else {

         /*
          * This CANNOT happen because we have rounded-up `hw_time_ns` to
          * the next second.
          */
      }
   }
   enable_interrupts_forced();
   clock_rstats.full_resync_count++;

   /*
    * We know that we need at most 10 seconds to compensate 1 second of drift,
    * which is the max we can get at boot-time. Now, just to be sure, wait 15s
    * and then check we have absolutely no drift measurable in seconds.
    */
   enable_preemption_nosched();
   kernel_sleep(15 * TIMER_HZ);
   drift = clock_get_second_drift2(true);
   abs_drift = (drift > 0 ? drift : -drift);

   if (abs_drift > 1) {

      /*
       * The absolute drift must be <= 1 here.
       * abs_drift > 1 is VERY UNLIKELY to happen, but everything is possible,
       * we have to handle it somehow. Just fail silently and let the rest of
       * the code in clock_drift_adj() compensate for the multi-second drift.
       */

      clock_rstats.full_resync_abs_drift_gt_1++;
      trace_printk(5, "abs_drift: %d sec, subsec sync FAILED!\n", abs_drift);
      goto fail;
   }

   if (abs_drift == 1) {

      clock_rstats.full_resync_fail_count++;

      if (++local_full_resync_fails > FULL_RESYNC_MAX_ATTEMPTS)
         panic("Time-management: drift (%d) must be zero after sync", drift);

      trace_printk(5, "abs_drift == 1: retry!\n");
      goto retry;
   }

   /* Default case: abs_drift == 0 */
   in_full_resync = false;
   clock_rstats.full_resync_success_count++;
   trace_printk(5, "full resync done");
   return true;

fail:
   in_full_resync = false;
   clock_rstats.full_resync_fail_count++;
   return false;
}

static void clock_multi_second_resync(int drift)
{
   static u64 last_resync_time_ns;
   static int last_drift_value;

   const int abs_drift = (drift > 0 ? drift : -drift);
   const int adj_val = (TS_SCALE / TIMER_HZ) / (drift > 0 ? -10 : 10);
   const int adj_ticks = abs_drift * TIMER_HZ * 10;

   u64 now;
   u64 time_gap_ns;
   u64 ticks_gap;
   u64 drift_time_ns;
   u64 drift_per_tick;
   u32 old_tick_duration;
   bool adjusted_tick_duration = false;

   trace_printk(5, "multi-sec resync: adj_val: %d, "
                   "adj_ticks: %d, tick ns: %d",
                   adj_val, adj_ticks, __tick_duration);

   disable_interrupts_forced();
   {
      old_tick_duration = __tick_duration;

      if (last_resync_time_ns) {

         /*
          * We already did a drift compensation. Can this be a sistemic
          * problem because __tick_duration is slightly incorrect?
          */

         if ((drift > 0) == (last_drift_value > 0)) {

            /*
             * Both the current and the last drift value have the same sign.
             * We should try to improve `__tick_duration` instead of
             * periodically compensate the constant drift.
             */
            now = __time_ns;
            time_gap_ns = now - last_resync_time_ns;
            ticks_gap = time_gap_ns / (TS_SCALE / TIMER_HZ);
            drift_time_ns = (u64)drift * BILLION;
            drift_per_tick = drift_time_ns / ticks_gap;

            /*
             * Compensate 75% of the measured drift_per_tick in order to avoid
             * over-compensating.
             */
            __tick_duration -= drift_per_tick/4*3;
            adjusted_tick_duration = true;
         }
      }

      /*
       * Set the adj val and the number of ticks for drift compensation
       * in the timer handler.
       */
      __tick_adj_val = adj_val;
      __tick_adj_ticks_rem = adj_ticks;

      /*
       * Save the time and the drift value for further analysis next time.
       */
      last_resync_time_ns = __time_ns;
      last_drift_value = drift;
   }
   enable_interrupts_forced();
   clock_rstats.multi_second_resync_count++;

   if (adjusted_tick_duration) {
      trace_printk(5, "time gap:   %" PRIu64 " ns", time_gap_ns);
      trace_printk(5, "ticks gap:  %" PRIu64, ticks_gap);
      trace_printk(5, "drift/tick: %" PRIu64 " ns", drift_per_tick);
      trace_printk(1, "adjust tick ns %u -> %u",
                      old_tick_duration, __tick_duration);
   }
}

static void check_drift_and_sync(void)
{
   int drift, abs_drift;
   int abs_drift_tolerance = 1;
   bool do_subsec_sync = true;

retry:
   if (clock_in_resync()) {
      trace_printk(5, "multi-sec resync: resync in progress, wait");
      kernel_sleep(5 * TIMER_HZ);
      goto retry;
   }

   if (clock_rstats.full_resync_fail_count >= 3) {

      static bool shown_warning_subsec_sync;

      if (!shown_warning_subsec_sync) {
         trace_printk(1, "WARNING: deactivate subsec resync");
         shown_warning_subsec_sync = true;
      }

      abs_drift_tolerance = 3;
      do_subsec_sync = false;
   }

   trace_printk(5, "Check for clock drift...");

   /* NOTE: this disables the preemption */
   drift = clock_get_second_drift2(false);
   abs_drift = (drift > 0 ? drift : -drift);

   trace_printk(5, "Clock drift: %d seconds", drift);

   if (abs_drift > abs_drift_tolerance) {
      adj_cnt++;
      trace_printk(4, "Detected %d seconds drift. Resync!", drift);
      clock_multi_second_resync(drift);
   }

   if (do_subsec_sync &&
         ((abs_drift == 1 && adj_cnt > 6) || first_sssync_failed))
   {

      /*
      * The periodic drift compensation works great even in the
      * "long run" but it's expected very slowly to accumulate with
      * time some sub-second drift that cannot be measured directly,
      * because of HW clock's 1s resolution. We'll inevitably end up
      * introducing some error while compensating the apparent 1 sec
      * drift (which, in reality was 1.01s, for example).
      *
      * To compensate even this 2nd-order problem, it's worth from time
      * to time to do a full-resync (also called sub-second resync).
      * This should happen less then once every 24 h, depending on how
      * accurate the PIT is.
      */

      enable_preemption_nosched(); /* note the clock_get_second_drift2() call */
      {
         if (clock_in_resync())
            goto retry;

         adj_cnt = 0;
         if (clock_sub_second_resync()) {
            first_sssync_failed = false;
         } else {
            trace_printk(5, "Subsec sync failed again, wait and retry");
            goto retry;
         }
      }
      disable_preemption();
   }

   enable_preemption();
}

static void clock_drift_adj()
{
   /* Sleep 1 second after boot, in order to get a real value of `__time_ns` */
   kernel_sleep(TIMER_HZ);

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
    *
    * The code doing this job is in the function clock_sub_second_resync().
    */

   if (clock_sub_second_resync()) {

      /*
       * Since we got here, everything is alright. There is no more clock drift.
       * Sleep some time and then start the actual infinite loop of this thread,
       * which will compensate any clock drifts that might occur as Tilck runs
       * for a long time.
       */

      kernel_sleep(clock_drift_adj_loop_delay);

   } else {

      /*
       * If we got here, clock_sub_second_resync() detected an abs_drift > 1,
       * which is an extremely unlikely event. Handling: enter the loop as
       * described in the positive case, just without sleeping first.
       * The multi-second drift will be detected and clock_multi_second_resync()
       * will be called to compensate for that. In addition to that, set the
       * `first_sssync_failed` variable to true forcing another sub-second sync
       * after the first (multi-second) one. Note: in this case the condition
       * `abs_drift >= 2` will be immediately hit.
       */

      first_sssync_failed = true;
   }

   while (true) {

      if (!clock_in_resync()) {

         /*
          * It makes sense to check for the clock drift ONLY when there are
          * NO already ongoing corrections.
          */

         check_drift_and_sync();
      }

      kernel_sleep(clock_drift_adj_loop_delay);
   }
}

int clock_get_second_drift(void)
{
   return clock_get_second_drift2(true);
}

#else

int clock_get_second_drift(void)
{
   return 0;
}

#endif // KRN_CLOCK_DRIFT_COMP

void init_system_time(void)
{
   struct datetime d;

#if KRN_CLOCK_DRIFT_COMP
      if (kthread_create(&clock_drift_adj, 0, NULL) < 0)
         printk("WARNING: unable to create a kthread for clock_drift_adj()\n");
#endif

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
      case CLOCK_REALTIME_COARSE:
         real_time_get_timespec(tp);
         break;

      case CLOCK_MONOTONIC:
      case CLOCK_MONOTONIC_COARSE:
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
      case CLOCK_REALTIME_COARSE:
      case CLOCK_MONOTONIC:
      case CLOCK_MONOTONIC_COARSE:
      case CLOCK_MONOTONIC_RAW:
      case CLOCK_PROCESS_CPUTIME_ID:
      case CLOCK_THREAD_CPUTIME_ID:

         *res = (struct k_timespec64) {
            .tv_sec = 0,
            .tv_nsec = BILLION/TIMER_HZ,
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
