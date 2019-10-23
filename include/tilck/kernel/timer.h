/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/kernel/hal.h>
#include <tilck/common/atomics.h>

extern u64 __ticks;

static ALWAYS_INLINE u64 get_ticks(void)
{
   u64 curr_ticks;
   uptr var;

   disable_interrupts(&var);
   {
      curr_ticks = __ticks;
   }
   enable_interrupts(&var);
   return curr_ticks;
}

int timer_irq_handler(regs_t *r);
void timer_set_freq(u32 hz);
void init_timer(void);
