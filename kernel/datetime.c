/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/string_util.h>

#include <tilck/kernel/datetime.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/sys_types.h>
#include <tilck/kernel/syscalls.h>

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

u32 days_per_month[12] =
{
   31, // jan
   28, // feb
   31, // mar
   30, // apr
   31, // may
   30, // jun
   31, // jul
   31, // aug
   30, // sep
   31, // oct
   30, // nov
   31, // dec
};


#if defined(__i386__) || defined(__x86_64__)

void cmos_read_datetime(datetime_t *out);

#endif

void read_system_clock_datetime(datetime_t *out)
{
   cmos_read_datetime(out);
}

time_t datetime_to_timestamp(datetime_t d)
{
   time_t result = 0;
   u32 year_day = 0;
   u32 year = d.year - 1900;

   d.month--;
   d.day--;

   for (int i = 0; i < d.month; i++)
      year_day += days_per_month[i];

   year_day += d.day;

   if (is_leap_year(d.year) && d.month > 1 /* 1 = feb */)
      year_day++;

   result += d.sec + d.min * 60 + d.hour * 3600 + year_day * 86400;
   result += (year - 70) * 31536000;

   /* fixes for the leap years */
   result += ((year - 69) / 4) * 86400 - ((year - 1) / 100) * 86400;
   result += ((year + 399) / 400) * 86400;

   return result;
}

static void real_time_get_timeval(struct timeval *tv)
{
   datetime_t d;
   read_system_clock_datetime(&d);
   tv->tv_sec = datetime_to_timestamp(d);

   /*
    * Yes, in Tilck we're NOT going to support any time soon a higher precision
    * than what the system clock offers (1 sec) for real time clocks. The reason
    * is that there is no simple way to implement that. Previously, we used
    * for `tv_usec`:
    *    (ticks % TIMER_HZ) * 1000000 / TIMER_HZ
    *
    * but that was completely wrong because the system clock (used for seconds)
    * and the ticks are not "in sync": that causes sometimes two consecutive
    * calls of this function to return
    *
    *    b.tv_sec == a.tv_sec   BUT WITH
    *    b.tv_usec < a.tv_usec
    *
    * Sure, for a non-monotonic clock it's acceptable, but it's much better to
    * just avoid doing that, given that the < sec information is not consistent.
    *
    * Naive solutions like using ticks to count the seconds as well CANNOT be
    * correct neither because the PIT cannot accurately tick with an "exact"
    * frequency of TIMER_HZ (which is typically 100). Therefore, Tilck's
    * approach is the following: make its real time clock be accurate but with
    * a low-resolution and, at the same time, offer a MONOTONIC clock with a
    * higher resolution (1 / TIMER_HZ) but less accurate in case it's used for
    * long periods.
    *
    * Of course, a much better solution would be to support functions like
    * adjtime() and periodically adjust the tick-based time by reading the time
    * from the CMOS clock. Of course, the adjustment is supposed to be done in
    * a way that the clock always move monotonically forward. Also, in case the
    * hardware (modern machines usually do) supports a better timer than the
    * classic PIT, it has to be used. Actually, the time clocks don't really
    * need to use `ticks` at all. It depends on what the hardware offers.
    * That's roughly what a full-featured kernel like Linux does, but in Tilck,
    * the point is to keep the things ultra-simple.
    */
   tv->tv_usec = 0;
}

static void monotonic_time_get_timeval(struct timeval *tv)
{
   /*
    * As explained above, this clock do have the `usec` value set (with a
    * resolution of 1/TIMER_HZ), but it is NOT accurate for long periods.
    */
   u64 ticks = get_ticks();
   tv->tv_sec = (time_t)(ticks / TIMER_HZ);
   tv->tv_usec = (ticks % TIMER_HZ) * 1000000 / TIMER_HZ;
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
            .tv_sec = 1,
            .tv_nsec = 0,
         };
         break;

      case CLOCK_MONOTONIC:
         tp = (struct timespec) {
            .tv_sec = 0,
            .tv_nsec = 1000000000/TIMER_HZ,
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
