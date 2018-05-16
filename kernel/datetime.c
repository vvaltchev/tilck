
#include <common/string_util.h>
#include <exos/datetime.h>

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

#if defined(__i386__) || defined(__x86_64__)

void cmos_read_datetime(datetime_t *out);

#endif

void read_system_clock_datetime(datetime_t *out)
{
   cmos_read_datetime(out);
}

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

