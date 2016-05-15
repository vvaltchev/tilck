

#include <commonDefs.h>
#include <stringUtil.h>
#include <term.h>
#include <irq.h>
#include <kmalloc.h>
#include <paging.h>

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
   set_kernel_stack(0xC01FFFFF);

   magic_debug_break();
   switch_to_usermode_asm((void*)0xC0120000, (void*) (0xC0120000 + 64*1024));

   //map_page(get_curr_page_dir(), 0xA0000000U, 0x120000, true, true);
   //map_page(get_curr_page_dir(), 0xA0000000U + 4096, 0x120000 + 4096, true, true);

   //map_page(get_curr_page_dir(), 0xA0010000, alloc_phys_page(), true, true);
   //switch_to_usermode_asm((void *) 0xA0000000, (void *) 0xA000FFFF);
}


void test1()
{
   //const char *str = "hello world in 3MB";
   //memcpy((void*) 0x300000, str, strlen(str) + 1);

   //map_page(&kernel_page_dir, 0x900000, 0x300000, true, true);
   //printk("[pagination test] string at 0x900000: %s\n", (const char *)0x900000);

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

   gdt_install();
   idt_install();
   irq_install();

   init_physical_page_allocator();
   init_paging();

   term_init();

   timer_phase(CLOCK_HZ);

   irq_install_handler(0, timer_handler);
   irq_install_handler(1, keyboard_handler);

   IRQ_set_mask(0); // mask the timer interrupt.

   show_hello_message();


   sti();
   init_kb();

   test1();
   switch_to_user_mode();

   while (1) {
      halt();
   }
}
