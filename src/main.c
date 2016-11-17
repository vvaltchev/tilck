

#include <commonDefs.h>
#include <stringUtil.h>
#include <term.h>
#include <irq.h>
#include <kmalloc.h>
#include <paging.h>
#include <debug_utils.h>

#define TIMER_FREQ_HZ 10

void gdt_install();
void idt_install();


void init_kb();
void timer_handler();
void keyboard_handler(regs *r);
void set_timer_freq(int hz);
void set_kernel_stack(uint32_t stack);
void switch_to_usermode_asm(void *entryPoint, void *stackAddr);

void switch_to_user_mode()
{
   void * const user_program_vaddr = (void *) 0x08000000U;
   void * const user_program_stack_vaddr = (void *) 0x0800FFF0U;
   void * const user_program_paddr = (void *) 0x120000;

   // Set up our kernel stack.
   set_kernel_stack(0xC01FFFF0);

   // maps 16 pages (64 KB) for the user program

   map_pages(get_curr_page_dir(),
             (uintptr_t)user_program_vaddr,
             (uintptr_t)user_program_paddr, 16, true, true);

   printk("pdir entries used = %i\n", debug_count_used_pdir_entries(get_curr_page_dir()));
   debug_dump_used_pdir_entries(get_curr_page_dir());

   switch_to_usermode_asm(user_program_vaddr, user_program_stack_vaddr);
}


void show_hello_message()
{
   printk("Hello from my kernel!\n");
}


void kmalloc_trivial_perf_test();
void kmalloc_perf_test();

void kmain() {

   term_init();
   show_hello_message();

   gdt_install();
   idt_install();
   irq_install();

   init_physical_page_allocator();
   init_paging();
   initialize_kmalloc();

   set_timer_freq(TIMER_FREQ_HZ);

   irq_install_handler(0, timer_handler);
   irq_install_handler(1, keyboard_handler);

   IRQ_set_mask(0); // mask the timer interrupt.

   sti();
   init_kb();

   //kmalloc_trivial_perf_test();
   kmalloc_perf_test();

   switch_to_user_mode();

   while (1) {
      halt();
   }
}
