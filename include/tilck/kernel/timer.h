/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

void kernel_sleep(u64 ticks);  /* sleep for `ticks` timer ticks (jiffies) */
void kernel_sleep_ms(u64 ms);  /* sleep for `ms` milliseconds */
void delay_us(u32 us);         /* busy-wait for `us` microseconds */

u64 get_ticks(void);
void init_timer(void);
