/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/common/datetime.h>

#include <tilck/kernel/sys_types.h>

#define MILLION                   (1000 * 1000)
#define BILLION            (1000 * 1000 * 1000)

#define TS_SCALE                        BILLION

extern const char *weekdays[7];
extern const char *months3[12];

struct clock_resync_stats {

   u32 full_resync_count;
   u32 full_resync_fail_count;
   u32 full_resync_success_count;
   u32 full_resync_abs_drift_gt_1;
   u32 multi_second_resync_count;
};

static inline bool is_leap_year(u32 year)
{
   return (!(year % 4) && (year % 100)) || !(year % 400);
}

u64 get_sys_time(void);
s64 get_timestamp(void);
void init_system_time(void);
int clock_get_second_drift(void);
bool clock_in_full_resync(void);
void real_time_get_timespec(struct k_timespec64 *tp);
void monotonic_time_get_timespec(struct k_timespec64 *tp);
void clock_get_resync_stats(struct clock_resync_stats *s);

static ALWAYS_INLINE struct timespec
to_timespec(struct k_timespec64 tp)
{
   struct timespec res;
   STATIC_ASSERT(sizeof(res.tv_nsec) == sizeof(res.tv_nsec));

   if (sizeof(res.tv_sec) == tp.tv_sec)
      return *(struct timespec *)&tp;

   res = (struct timespec) {
      .tv_sec = (s32)tp.tv_sec,
      .tv_nsec = tp.tv_nsec,
   };

   return res;
}

static ALWAYS_INLINE struct k_timespec64
from_timespec(struct timespec tp)
{
   struct k_timespec64 res;
   STATIC_ASSERT(sizeof(res.tv_nsec) == sizeof(res.tv_nsec));

   if (sizeof(res.tv_sec) == tp.tv_sec)
      return *(struct k_timespec64 *)&tp;

   res = (struct k_timespec64) {
      .tv_sec = tp.tv_sec,
      .tv_nsec = tp.tv_nsec,
   };

   return res;
}
