

#include <commonDefs.h>
#include <stringUtil.h>
#include <term.h>
#include <irq.h>

void gdt_install();
void idt_install();

void timer_phase(int hz)
{
   int divisor = 1193180 / hz;   /* Calculate our divisor */
   outb(0x43, 0x36);             /* Set our command byte 0x36 */
   outb(0x40, divisor & 0xFF);   /* Set low byte of divisor */
   outb(0x40, divisor >> 8);     /* Set high byte of divisor */
}


/* This will keep track of how many ticks that the system
*  has been running for */
volatile unsigned timer_ticks = 0;

#define CLOCK_HZ 10

/* Handles the timer. In this case, it's very simple: We
*  increment the 'timer_ticks' variable every time the
*  timer fires. By default, the timer fires 18.222 times
*  per second. Why 18.222Hz? Some engineer at IBM must've
*  been smoking something funky */


void timer_handler()
{
   unsigned val = ++timer_ticks;

   if ((val % CLOCK_HZ) == 0) {
      printk("Ticks: %u\n", timer_ticks);
   }
}

void init_kb();

void keyboard_handler(struct regs *r);


#include "usermode_syscall_wrappers.h"

void set_kernel_stack(uint32_t stack);

void switch_to_usermode_asm(void *entryPoint, void *stackAddr);

void usermode_init()
{
   for (int i = 0; i < 10; ++i) {
      asmVolatile("");
   }

   int ret = generic_usermode_syscall_wrapper3(5, "/myfile.txt", (void*)0xAABB, (void*)0x112233);

   while (ret > 0) {

      generic_usermode_syscall_wrapper3(4, 2, "hello", 0);

      ret--;
   }

   while (1);
}

void switch_to_user_mode()
{
   // Set up our kernel stack.
   set_kernel_stack(0x1FFFFF);

   switch_to_usermode_asm(usermode_init, (void*) 0x2FFFFF);
}


void kmain() {

   term_init();
   show_hello_message();

   gdt_install();
   idt_install();
   irq_install();

   timer_phase(CLOCK_HZ);

   irq_install_handler(0, timer_handler);
   irq_install_handler(1, keyboard_handler);

   IRQ_set_mask(0); // mask the timer interrupt.

   sti();
   init_kb();


   ///////////////////////////////////////////////////////////

   switch_to_user_mode();
   //////////////////

   while (1) {
      halt();
   }
}
