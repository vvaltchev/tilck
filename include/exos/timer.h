
#pragma once

#include <exos/hal.h>

#define TIMER_HZ 100
#define TIME_SLOT_JIFFIES (TIMER_HZ / 20)

void timer_handler(regs *r);
void timer_set_freq(int hz);
