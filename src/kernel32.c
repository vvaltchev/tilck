

#include <commonDefs.h>
#include <stringUtil.h>
#include <term.h>
#include <irq.h>
#include <kmalloc.h>

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



void set_kernel_stack(uint32_t stack);

void switch_to_usermode_asm(void *entryPoint, void *stackAddr);



void switch_to_user_mode()
{
   // Set up our kernel stack.
   set_kernel_stack(0x1FFFFF);

   switch_to_usermode_asm((void*)0x108000, (void*) (0x108000 + 128*1024));
}


void test1()
{
   void *p[64];
   int i;

   for (i = 0; i < 33; i++)
      p[i] = alloc_phys_page();

   for (i = 0; i < 33; i++)
      free_phys_page(p[i]);

   alloc_phys_page();
}

void show_hello_message()
{
   printk("Hello from my kernel!\n");
}

void panic(const char *fmt, ...)
{
   cli();

   printk("\n\n************** KERNEL PANIC **************\n");

   va_list args;
   va_start(args, fmt);
   vprintk(fmt, args);
   va_end(args);

   while (true) {
      halt();
   }
}

void assert_failed(const char *expr, const char *file, int line)
{
   panic("\nASSERTION '%s' FAILED in file '%s' at line %i\n",
         expr, file, line);
}

void kmain() {

   init_physical_page_allocator();
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

   //test1();
   //switch_to_user_mode();

   for (int i = 1; i < 24; i++) printk("%i\n", i);

   while (1) {
      halt();
   }
}
