/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/kernel/hal.h>

#define TIME_SLOT_TICKS (TIMER_HZ / 20)

extern volatile u64 __ticks;

static ALWAYS_INLINE u64 get_ticks(void)
{
   return __ticks;
}

int timer_irq_handler(regs *r);
void timer_set_freq(int hz);
void init_timer(void);
