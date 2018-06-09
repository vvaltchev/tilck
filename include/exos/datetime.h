
#pragma once
#include <common/basic_defs.h>

typedef struct {

   union {

      struct {
         u8 sec;
         u8 min;
         u8 hour;
         u8 weekday;
         u8 day;
         u8 month;
         u16 year;
      };

      u64 raw;
   };

} datetime_t;

typedef uptr time_t;
typedef uptr suseconds_t;

typedef struct {
   time_t      tv_sec;     /* seconds */
   suseconds_t tv_usec;    /* microseconds */
} timeval;

typedef struct {
   int tz_minuteswest;     /* minutes west of Greenwich */
   int tz_dsttime;         /* type of DST correction */
} timezone;

extern const char *weekdays[7];

static inline bool is_leap_year(u32 year)
{
   return (!(year % 4) && (year % 100)) || !(year % 400);
}

void print_datetime(datetime_t d);
void read_system_clock_datetime(datetime_t *out);
