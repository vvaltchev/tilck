/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/common/datetime.h>

#include <tilck/kernel/sys_types.h>

extern const char *weekdays[7];
extern const char *months3[12];

static inline bool is_leap_year(u32 year)
{
   return (!(year % 4) && (year % 100)) || !(year % 400);
}

void read_system_clock_datetime(struct datetime *out);
s64 read_system_clock_timestamp(void);
void init_system_clock(void);
