
#include <common_defs.h>
#include <process.h>
#include <arch/generic_x86/utils.h>

/*
 * Sets timer's frequency.
 * Default value: 18.222 Hz.
 */

void set_timer_freq(int hz)
{
   ASSERT(hz >= 1 && hz <= 1000);

   int divisor = 1193180 / hz;   /* Calculate our divisor */
   outb(0x43, 0x36);             /* Set our command byte 0x36 */
   outb(0x40, divisor & 0xFF);   /* Set low byte of divisor */
   outb(0x40, divisor >> 8);     /* Set high byte of divisor */
}


/*
 * This will keep track of how many ticks that the system
 * has been running for.
 */
volatile u64 jiffies = 0;

extern volatile task_info *current_process;

void timer_handler(regs *r)
{
   jiffies++;

   if (!current_process) {
      // The kernel is still initializing and we cannot call schedule() yet.
      return;
   }

   save_current_process_state(r);
   schedule();
}

