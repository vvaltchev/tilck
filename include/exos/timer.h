
#pragma once

#include <exos/hal.h>

#define TIMER_HZ 250

void timer_handler(regs *r);
void timer_set_freq(int hz);
