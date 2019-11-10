/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/string_util.h>

#include <tilck/kernel/datetime.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/sys_types.h>
#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/process.h>

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
extern u64 __time_ns;
extern u32 __tick_duration;

void init_system_time(void)
{
   struct datetime d;
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
         tp = (struct timespec) {
            .tv_sec = 0,
            .tv_nsec = BILLION/TIMER_HZ,
         };
         break;

      case CLOCK_MONOTONIC:
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
