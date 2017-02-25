

#include <common_defs.h>
#include <string_util.h>
#include <term.h>
#include <irq.h>
#include <kmalloc.h>
#include <paging.h>
#include <debug_utils.h>
#include <process.h>

#include <arch_utils.h>

void gdt_install();
void idt_install();


void init_kb();
void timer_handler(regs *r);
void keyboard_handler(regs *r);
void set_timer_freq(int hz);


void load_elf_program(void *elf,
                      page_directory_t *pdir,
                      void **entry,
                      void **stack_addr);


#define INIT_PROGRAM_PHYSICAL_ADDR 0x120000UL

void run_usermode_init()
{
   void *elf_vaddr = (void *)0xB0000000U;

   // Map somewhere in kernel space the ELF of our init program

   map_pages(get_kernel_page_dir(),
             elf_vaddr,
             INIT_PROGRAM_PHYSICAL_ADDR, 16, false, false);

   page_directory_t *pdir = pdir_clone(get_kernel_page_dir());

   set_page_directory(pdir);

   void *entry_point = NULL;
   void *stack_addr = NULL;
   load_elf_program(elf_vaddr, pdir, &entry_point, &stack_addr);

   // Unmap the temporary mapping of the ELF file in kernel space.
   unmap_pages(get_kernel_page_dir(), elf_vaddr, 16);

   printk("[run_usermode_init] Entry: %p\n", entry_point);
   printk("[run_usermode_init] Stack: %p\n", stack_addr);

   first_usermode_switch(pdir, entry_point, stack_addr);
}

void show_hello_message()
{
   printk("Hello from my kernel!\n");
}

void kmain()
{
   term_init();
   show_hello_message();

   gdt_install();
   idt_install();
   irq_install();

   init_pageframe_allocator();
   init_paging();
   initialize_kmalloc();

   set_timer_freq(TIMER_HZ);
   irq_install_handler(0, timer_handler);
   irq_install_handler(1, keyboard_handler);

   set_kernel_stack(0xC01FFFF0);

   setup_syscall_interface();

   // Restore the interrupts.
   sti();

   // Initialize the keyboard driver.
   init_kb();

   // Run the 'init' usermode program.
   //run_usermode_init();

   // We should never get here!
   NOT_REACHED();
}
