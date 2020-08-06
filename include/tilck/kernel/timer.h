/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

void kernel_sleep(u64 ticks);
void kernel_sleep_ms(u64 ms);

u64 get_ticks(void);
void init_timer(void);
