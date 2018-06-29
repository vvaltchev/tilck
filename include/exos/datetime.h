
#pragma once
#include <common/basic_defs.h>
#include <exos/sys_types.h>

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

extern const char *weekdays[7];
extern const char *months3[12];

static inline bool is_leap_year(u32 year)
{
   return (!(year % 4) && (year % 100)) || !(year % 400);
}

void print_datetime(datetime_t d);
void read_system_clock_datetime(datetime_t *out);
uptr datetime_to_timestamp(datetime_t d);
