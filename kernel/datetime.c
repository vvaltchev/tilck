
#include <common/string_util.h>

#include <exos/datetime.h>
#include <exos/user.h>
#include <exos/errno.h>
#include <exos/timer.h>

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

void print_datetime(datetime_t d)
{
   printk("date & time: %s %s%i/%s%i/%i %s%i:%s%i:%s%i\n",
          weekdays[d.weekday - 1],
          d.day < 10 ? "0" : "",
          d.day,
          d.month < 10 ? "0" : "",
          d.month,
          d.year,
          d.hour < 10 ? "0" : "",
          d.hour,
          d.min < 10 ? "0" : "",
          d.min,
          d.sec < 10 ? "0" : "",
          d.sec);
}

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

sptr sys_gettimeofday(timeval *user_tv, timezone *user_tz)
{
   datetime_t d;
   timezone tz;
   timeval tv;
   int rc;

   read_system_clock_datetime(&d);

   tv.tv_sec = datetime_to_timestamp(d);
   tv.tv_usec = (get_ticks() % TIMER_HZ) * 1000 / TIMER_HZ;

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

