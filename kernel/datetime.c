/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/string_util.h>

#include <tilck/kernel/datetime.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/sys_types.h>
#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/hal.h>

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

static s64 __boot_time_s;
extern u64 __time_us;

void cmos_read_datetime(struct datetime *out);

void init_system_time(void)
{
   struct datetime d;
   cmos_read_datetime(&d);
   __boot_time_s = datetime_to_timestamp(d);

   if (__boot_time_s < 0)
      panic("Invalid boot-time UNIX timestamp: %d\n", __boot_time_s);

   __time_us = 0;
}

u64 get_sys_time(void)
{
   u64 ts;
   uptr var;
   disable_interrupts(&var);
   {
      ts = __time_us;
   }
   enable_interrupts(&var);
   return ts;
}

s64 get_timestamp(void)
{
   u64 ts = get_sys_time();
   return __boot_time_s + (s64)(ts / TS_SCALE);
}

static void real_time_get_timeval(struct timeval *tv)
{
   u64 ts = ((u64)__boot_time_s) * TS_SCALE + get_sys_time();

   tv->tv_sec = (time_t)(ts / TS_SCALE);

   if (TS_SCALE <= MILLION)
      tv->tv_usec = (ts % TS_SCALE) * (MILLION / TS_SCALE);
   else
      tv->tv_usec = (ts % TS_SCALE) / (TS_SCALE / MILLION);
}

static void monotonic_time_get_timeval(struct timeval *tv)
{
   /* Same as the real_time clock, for the moment */
   real_time_get_timeval(tv);
}

int sys_gettimeofday(struct timeval *user_tv, struct timezone *user_tz)
{
   struct timeval tv;
   struct timezone tz = {
      .tz_minuteswest = 0,
      .tz_dsttime = 0,
   };

   real_time_get_timeval(&tv);

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
   struct timeval tv;
   struct timespec tp;

   if (!user_tp)
      return -EINVAL;

   switch (clk_id) {
      case CLOCK_REALTIME:
         real_time_get_timeval(&tv);
         break;

      case CLOCK_MONOTONIC:
         monotonic_time_get_timeval(&tv);
         break;

      default:
         printk("WARNING: unsupported clk_id: %d\n", clk_id);
         return -EINVAL;
   }

   tp.tv_sec = tv.tv_sec;
   tp.tv_nsec = tv.tv_usec * 1000;

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
