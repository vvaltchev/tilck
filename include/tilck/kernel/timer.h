/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck_gen_headers/config_sched.h>
#include <tilck/common/basic_defs.h>

void kernel_sleep(u64 ticks);  /* sleep for `ticks` timer ticks (jiffies) */
void kernel_sleep_ms(u64 ms);  /* sleep for `ms` milliseconds */
void delay_us(u32 us);         /* busy-wait for `us` microseconds */

static ALWAYS_INLINE u64
ms_to_ticks(u64 ms)
{
   return ms / (1000 / TIMER_HZ);
}

u64 get_ticks(void);
void init_timer(void);
