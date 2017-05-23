

#include <common_defs.h>
#include <string_util.h>
#include <term.h>
#include <irq.h>
#include <kmalloc.h>
#include <paging.h>
#include <debug_utils.h>
#include <process.h>

#include <hal.h>
#include <utils.h>
#include <tasklet.h>
#include <sync.h>

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


#define INIT_PROGRAM_MEM_DISK_OFFSET 0x00023600

task_info *usermode_init_task = NULL;

task_info *load_usermode_init()
{
   void *elf_vaddr = (void *) (RAM_DISK_VADDR + INIT_PROGRAM_MEM_DISK_OFFSET);

   page_directory_t *pdir = pdir_clone(get_kernel_page_dir());
   set_page_directory(pdir);

   void *entry_point = NULL;
   void *stack_addr = NULL;
   load_elf_program(elf_vaddr, pdir, &entry_point, &stack_addr);

   printk("[load_usermode_init] Entry: %p\n", entry_point);
   printk("[load_usermode_init] Stack: %p\n", stack_addr);

   return create_first_usermode_task(pdir, entry_point, stack_addr);
}

void show_hello_message()
{
   printk("Hello from exOS!\n");
}

void mount_memdisk()
{
   printk("Mapping the vdisk at %p (%d pages)...\n",
          RAM_DISK_VADDR, RAM_DISK_SIZE / PAGE_SIZE);

   map_pages(get_kernel_page_dir(),
             (void *) RAM_DISK_VADDR,
             RAM_DISK_PADDR,
             RAM_DISK_SIZE / PAGE_SIZE,
             false,
             true);
}


void sleeping_kthread(void *);
void simple_test_kthread(void *);
void kmutex_test();
void kcond_test();

void kmain()
{
   term_init();
   show_hello_message();

   gdt_install();
   idt_install();
   irq_install();

   init_pageframe_allocator();
   initialize_kmalloc();

   init_paging();

   initialize_scheduler();
   initialize_tasklets();

   set_timer_freq(TIMER_HZ);

   irq_install_handler(X86_PC_TIMER_IRQ, timer_handler);
   irq_install_handler(X86_PC_KEYBOARD_IRQ, keyboard_handler);

   // TODO: make the kernel actually support the sysenter interface
   setup_sysenter_interface();

   mount_memdisk();
   //test_memdisk();

   // Initialize the keyboard driver.
   init_kb();

   // kthread_create(&simple_test_kthread, (void*)0xAA1234BB);
   // kmutex_test();
   // kcond_test();
   // task_info *t1 = kthread_create(&sleeping_kthread, (void *) 123);
   // task_info *t2 = kthread_create(&sleeping_kthread, (void *) 20);
   // kthread_create(&sleeping_kthread, (void *) (10*TIMER_HZ));

   usermode_init_task = load_usermode_init();
   switch_to_task_outside_interrupt_context(usermode_init_task);

   // (void)t1;
   // (void)t2;

   // switch_to_task_outside_interrupt_context(t1);

   // We should never get here!
   NOT_REACHED();
}
