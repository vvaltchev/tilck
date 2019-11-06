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

static struct datetime __current_datetime;
void cmos_read_datetime(struct datetime *out);

/*
 * The purpose of this thread is to keep the real date-time cheap to access,
 * by updating it once per second.
 *
 * Note: cmos_read_datetime() costs about ~270,000 cycles on a VM. On real HW
 * probably is cheaper, but it's still expensive as most of the operations
 * involving I/O ports.
 *
 * Certainly, there are much better solutions for this problem, but none of them
 * are simply enough and convenient for Tilck. For example, the function below
 * read_system_clock_datetime() could call cmos_read_datetime() only when too
 * many ticks (e.g. > TIMER_HZ/2) elapsed since the last CMOS read. That
 * approach is extremely simple and efficient but it leads to non-linear
 * datetime readings because the CMOS clock and the timer ticks are *not* in
 * sync (e.g. we didn't start the timer at hh.mm.ss.000) the hidden sub-second
 * time difference between the CMOS clock and what read_system_clock_datetime()
 * could return as datetime varies every time more that the given a certain N
 * ticks (e.g. TIMER_HZ/2) elapsed since the last call of that function.
 * Synchronizing the ticks and the CMOS clock is possible, but it is expensive
 * and it won't last forever, as the system timer cannot tick with exact
 * TIMER_HZ frequence.
 *
 * As mentioned below in real_time_get_timeval(), it is pretty complex to get
 * accurate and cheap time readings. Ideally, we should read from time to time
 * the CMOS clock (once in a few minutes for example) and it the meanwhile we
 * should use system timer's ticks to "interpolate" the time. But this is
 * complex to implement because:
 *
 *    1) What happens if CMOS clock gets behind us? We have to gradually "lag"
 *       until we compensate that difference, like the Linux kernel does with
 *       adjtimex(), while keeping the time increase monotonically. That is
 *       complex to implement correctly.
 *
 *    2) In order to "interpolate" time, we'd need to use a timestamp instead of
 *       struct datetime. But, unfortunately, our fb_console needs to know the
 *       date and time. Support that would mean implementing a "hairy" function
 *       to convert timestamp to struct datetime considering all the tricky
 *       leap years stuff and maybe leap seconds too?
 *
 *    3) Other problems.
 *
 * In order to keep Tilck simple, at least for the moment, this time management
 * is considered "good enough".
 */
static void clock_update_thread()
{
   struct datetime tmp;

   while (true) {

      cmos_read_datetime(&tmp);
      disable_preemption();
      {
          __current_datetime.raw = tmp.raw;
      }
      enable_preemption();
      kernel_sleep(TIMER_HZ);
   }
}

void init_system_clock(void)
{
   if (kthread_create(clock_update_thread, NULL) < 0)
      panic("Unable to create the clock update kthread");
}

void read_system_clock_datetime(struct datetime *out)
{
   if (UNLIKELY(__current_datetime.year == 0)) {

      /*
       * Special case: we've been called before clock_update_thread() had any
       * chance to run. Read the date-time from the CMOS clock and update the
       * global variable.
       */

      cmos_read_datetime(out);

      disable_preemption();
      {
         __current_datetime.raw = out->raw;
      }
      enable_preemption();
      return;
   }

   disable_preemption();
   {
      out->raw = __current_datetime.raw;
   }
   enable_preemption();
}

s64 read_system_clock_timestamp(void)
{
   struct datetime dt;
   read_system_clock_datetime(&dt);
   return datetime_to_timestamp(dt);
}

static void real_time_get_timeval(struct timeval *tv)
{
   struct datetime d;
   read_system_clock_datetime(&d);
   tv->tv_sec = (time_t)datetime_to_timestamp(d);

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
    * Sure, for a non-monotonic clock it's theoretically acceptable, but it's
    * just does NOT make any sense.
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
