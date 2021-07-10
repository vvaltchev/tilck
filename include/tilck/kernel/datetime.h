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
bool clock_in_resync(void);
bool clock_in_full_resync(void);
void ticks_to_timespec(u64 ticks, struct k_timespec64 *tp);
u64 timespec_to_ticks(const struct k_timespec64 *tp);
void real_time_get_timespec(struct k_timespec64 *tp);
void monotonic_time_get_timespec(struct k_timespec64 *tp);
void clock_get_resync_stats(struct clock_resync_stats *s);

union k_compat_timespec_u {
   struct k_timespec64 ts64;
   struct timespec ts;
};

static ALWAYS_INLINE struct timespec
to_timespec(struct k_timespec64 tp)
{
   union k_compat_timespec_u res;
   STATIC_ASSERT(sizeof(res.ts.tv_nsec) == sizeof(res.ts64.tv_nsec));

   if (sizeof(res.ts.tv_sec) != sizeof(res.ts64.tv_sec)) {

      res.ts = (struct timespec) {
         .tv_sec = (s32)tp.tv_sec, // NOTE: truncation
         .tv_nsec = tp.tv_nsec,
      };

   } else {

      /* We're on a 64-bit architecture and ts and ts64 are the same type */
      res.ts64 = tp;
   }

   return res.ts;
}

static ALWAYS_INLINE struct k_timespec64
from_timespec(struct timespec tp)
{
   union k_compat_timespec_u res;
   STATIC_ASSERT(sizeof(res.ts.tv_nsec) == sizeof(res.ts64.tv_nsec));

   if (sizeof(res.ts.tv_sec) != sizeof(res.ts64.tv_sec)) {

      res.ts64 = (struct k_timespec64) {
         .tv_sec = tp.tv_sec,    // NOTE: extension
         .tv_nsec = tp.tv_nsec,
      };

   } else {

      /* We're on a 64-bit architecture and ts and ts64 are the same type */
      res.ts = tp;
   }

   return res.ts64;
}
