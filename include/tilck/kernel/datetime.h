/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/sys_types.h>

#include <time.h> // system header

struct datetime {

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
};

extern const char *weekdays[7];
extern const char *months3[12];

static inline bool is_leap_year(u32 year)
{
   return (!(year % 4) && (year % 100)) || !(year % 400);
}

void read_system_clock_datetime(struct datetime *out);
time_t read_system_clock_timestamp(void);
time_t datetime_to_timestamp(struct datetime d);
void init_system_clock(void);
