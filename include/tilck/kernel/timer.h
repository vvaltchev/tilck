/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/atomics.h>
#include <tilck/kernel/hal_types.h>

u64 get_ticks(void);
int timer_irq_handler(regs_t *r);
void timer_set_freq(u32 hz);
void init_timer(void);
