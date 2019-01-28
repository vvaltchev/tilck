/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/timer.h>

/*
 * Sets timer's frequency.
 * Default value: 18.2 Hz.
 */

void timer_set_freq(u32 hz)
{
   ASSERT(hz >= 18 && hz <= 1000);

   // TODO: replace integer constants with MACROs

   u32 divisor = 1193180 / hz;            /* Calculate our divisor */
   outb(0x43, 0x36);                      /* Set our command byte 0x36 */
   outb(0x40, divisor & 0xff);            /* Set low byte of divisor */
   outb(0x40, (divisor >> 8) & 0xff);     /* Set high byte of divisor */
}
