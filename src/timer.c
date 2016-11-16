
#include <commonDefs.h>

/*
 * Sets timer's frequency.
 * Default value: 18.222 Hz.
 */

void set_timer_freq(int hz)
{
   ASSERT(hz >= 1 && hz <= 250);

   int divisor = 1193180 / hz;   /* Calculate our divisor */
   outb(0x43, 0x36);             /* Set our command byte 0x36 */
   outb(0x40, divisor & 0xFF);   /* Set low byte of divisor */
   outb(0x40, divisor >> 8);     /* Set high byte of divisor */
}


/*
 * This will keep track of how many ticks that the system
 * has been running for.
 */
volatile uint32_t timer_ticks = 0;


void timer_handler() {
   timer_ticks++;
}