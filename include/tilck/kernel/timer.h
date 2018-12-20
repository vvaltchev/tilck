/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/kernel/hal.h>
#include <tilck/common/atomics.h>

#define TIME_SLOT_TICKS (TIMER_HZ / 20)

extern ATOMIC(u64) __ticks;

static ALWAYS_INLINE u64 get_ticks(void)
{
   return atomic_load_explicit(&__ticks, mo_relaxed);
}

int timer_irq_handler(regs *r);
void timer_set_freq(int hz);
void init_timer(void);
