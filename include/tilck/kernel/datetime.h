/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/common/datetime.h>

#include <tilck/kernel/sys_types.h>

#define MILLION                   (1000 * 1000)
#define BILLION            (1000 * 1000 * 1000)

#define TS_SCALE                        MILLION

extern const char *weekdays[7];
extern const char *months3[12];

static inline bool is_leap_year(u32 year)
{
   return (!(year % 4) && (year % 100)) || !(year % 400);
}

u64 get_sys_time(void);
s64 get_timestamp(void);
void init_system_time(void);
