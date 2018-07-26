
#include <tilck/common/string_util.h>

#include <tilck/kernel/datetime.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/sys_types.h>

#include <time.h> // system header

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
   "Dec"
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
   31  // dec
};


#if defined(__i386__) || defined(__x86_64__)

void cmos_read_datetime(datetime_t *out);

#endif

void read_system_clock_datetime(datetime_t *out)
{
   cmos_read_datetime(out);
}

uptr datetime_to_timestamp(datetime_t d)
{
   uptr result = 0;
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

static void read_timeval(struct timeval *tv)
{
   datetime_t d;

   read_system_clock_datetime(&d);
   tv->tv_sec = datetime_to_timestamp(d);
   tv->tv_usec = (get_ticks() % TIMER_HZ) * 1000 / TIMER_HZ;
}

sptr sys_gettimeofday(struct timeval *user_tv, struct timezone *user_tz)
{
   struct timezone tz;
   struct timeval tv;
   int rc;

   read_timeval(&tv);
   tz.tz_minuteswest = 0;
   tz.tz_dsttime = 0;

   if (user_tv) {
      rc = copy_to_user(user_tv, &tv, sizeof(tv));

      if (rc < 0)
         return -EFAULT;
   }

   if (user_tz) {
      rc = copy_to_user(user_tz, &tz, sizeof(tz));

      if (rc < 0)
         return -EFAULT;
   }

   return 0;
}

sptr sys_clock_gettime(clockid_t clk_id, struct timespec *user_tp)
{
   if (clk_id == CLOCK_REALTIME) {

      struct timeval tv;
      struct timespec tp;

      if (!user_tp)
         return -EINVAL;

      read_timeval(&tv);
      tp.tv_sec = tv.tv_sec;
      tp.tv_nsec = tv.tv_usec * 1000;

      int rc = copy_to_user(user_tp, &tp, sizeof(tp));

      if (rc < 0)
         return -EFAULT;

      return 0;
   }

   printk("WARNING: unsupported clk_id: %d\n", clk_id);
   return -EINVAL;
}
