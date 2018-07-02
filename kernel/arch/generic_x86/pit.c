
#include <common/basic_defs.h>
#include <exos/hal.h>

/*
 * Sets timer's frequency.
 * Default value: 18.2 Hz.
 */

void timer_set_freq(int hz)
{
   ASSERT(hz >= 18 && hz <= 1000);

   int divisor = 1193180 / hz;   /* Calculate our divisor */
   outb(0x43, 0x36);             /* Set our command byte 0x36 */
   outb(0x40, divisor & 0xFF);   /* Set low byte of divisor */
   outb(0x40, divisor >> 8);     /* Set high byte of divisor */
}
