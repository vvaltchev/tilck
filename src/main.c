

#include <commonDefs.h>
#include <stringUtil.h>
#include <term.h>
#include <irq.h>
#include <kmalloc.h>
#include <paging.h>
#include <debug_utils.h>

#define TIMER_FREQ_HZ 100

void gdt_install();
void idt_install();


void init_kb();
void timer_handler(regs *r);
void keyboard_handler(regs *r);
void set_timer_freq(int hz);
void set_kernel_stack(uint32_t stack);
void switch_to_usermode_asm(void *entryPoint, void *stackAddr);

void load_usermode_init()
{
   void *const vaddr = (void *)0x08000000U;
   void *const paddr = (void *)0x120000;

   page_directory_t *pdir = pdir_clone(get_kernel_page_dir());

   // maps 16 pages (64 KB) for the user program

   map_pages(pdir,
             (uintptr_t)vaddr,
             (uintptr_t)paddr, 16, true, true);

   // map 4 pages for the user program's stack

   map_pages(pdir,
             (uintptr_t)vaddr + 16 * PAGE_SIZE,
             (uintptr_t)paddr + 16 * PAGE_SIZE, 4, true, true);


   void *stack = (void *) (((uintptr_t)vaddr + (16 + 4) * PAGE_SIZE - 1) & ~15);

   // simulate a page directory of fork-ed process
   // to observe COW working
   page_directory_t *pdir2 = pdir_clone(pdir);

   set_page_directory(pdir2);

   printk("user mode stack addr: %p\n", stack);
   switch_to_usermode_asm(vaddr, stack);
}

void switch_to_user_mode()
{
   // Set up our kernel stack.
   set_kernel_stack(0xC01FFFF0);

   load_usermode_init();

   //printk("pdir entries used = %i\n", debug_count_used_pdir_entries(get_curr_page_dir()));
   //debug_dump_used_pdir_entries(get_curr_page_dir());

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

   //IRQ_set_mask(0); // mask the timer interrupt.

   sti();
   init_kb();

   //kmalloc_perf_test();

   switch_to_user_mode();

   while (1) {
      halt();
   }
}
