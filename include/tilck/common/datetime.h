/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

struct datetime {

   union {

      struct {
         u8 sec;        /* Seconds (0-60) */
         u8 min;        /* Minutes (0-59) */
         u8 hour;       /* Hours (0-23) */
         u8 weekday;    /* Weekday (0-6, Sunday = 0) */
         u8 day;        /* Month day (1 - 31) */
         u8 month;      /* Month (1 - 12) */
         u16 year;      /* Absolute year (e.g. 1542, 2019, 2059, ...) */
      };

      u64 raw;
   };
};

int timestamp_to_datetime(int64_t t, struct datetime *d);
int64_t datetime_to_timestamp(struct datetime d);
