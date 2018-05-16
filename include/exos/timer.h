
#pragma once

#include <exos/hal.h>

#define TIMER_HZ 100
#define TIME_SLOT_JIFFIES (TIMER_HZ / 20)

typedef struct {

   u64 ticks_to_sleep;
   void *task;     // task == NULL means that the slot is unused.

} kthread_timer_sleep_obj;


extern volatile u64 jiffies;

static ALWAYS_INLINE u64 get_ticks(void)
{
   return jiffies;
}

void timer_handler(regs *r);
void timer_set_freq(int hz);
