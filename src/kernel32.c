

#include <commonDefs.h>
#include <stringUtil.h>
#include <term.h>
#include <irq.h>
#include <kmalloc.h>
#include <paging.h>
#include <debug_utils.h>

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
   set_kernel_stack(0xC01FFFF0);

   // maps 16 pages for the user program
   for (int i = 0; i < 16; i++) {
      map_page(get_curr_page_dir(),
               0x08000000U + 4096 * i,
               0x120000 + 4096 * i,
               true,  // US
               true); // RW
   }

   printk("pdir entries used = %i\n", debug_count_used_pdir_entries(get_curr_page_dir()));
   debug_dump_used_pdir_entries(get_curr_page_dir());

   magic_debug_break();

   // test

   //void *paddr = alloc_phys_page();
   //map_page(get_curr_page_dir(), 0xB0000000, (uint32_t) paddr, true, false);
   //volatile char *robuf = (volatile char *)0xB0000000;

   //robuf[123] = 'x';

   ///////////////

   switch_to_usermode_asm((void *) 0x08000000, (void *) 0x0800FFF0);
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

   printk("\n");
   dump_stacktrace();

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

   term_init();
   show_hello_message();

   gdt_install();
   idt_install();
   irq_install();

   init_physical_page_allocator();
   init_paging();


   timer_phase(CLOCK_HZ);

   irq_install_handler(0, timer_handler);
   irq_install_handler(1, keyboard_handler);

   IRQ_set_mask(0); // mask the timer interrupt.

   sti();
   init_kb();

   switch_to_user_mode();

   while (1) {
      halt();
   }
}
