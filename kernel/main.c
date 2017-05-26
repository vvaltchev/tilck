

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

// TODO: move these forward-declarations in appropriate header files.

void gdt_install();
void idt_install();

void init_kb();
void timer_handler(regs *r);
void keyboard_handler(regs *r);
void set_timer_freq(int hz);

void sleeping_kthread(void *);
void simple_test_kthread(void *);
void kmutex_test();
void kcond_test();
void tasklet_stress_test();

void load_elf_program(void *elf,
                      page_directory_t *pdir,
                      void **entry,
                      void **stack_addr);



void show_hello_message()
{
   printk("Hello from exOS!\n");
   printk("TIMER_HZ: %i\n", TIMER_HZ);
}



#define INIT_PROGRAM_MEM_DISK_OFFSET 0x00023600

task_info *usermode_init_task = NULL;

void load_usermode_init()
{
   void *elf_vaddr = (void *) (RAM_DISK_VADDR + INIT_PROGRAM_MEM_DISK_OFFSET);

   page_directory_t *pdir = pdir_clone(get_kernel_page_dir());
   set_page_directory(pdir);

   void *entry_point = NULL;
   void *stack_addr = NULL;
   load_elf_program(elf_vaddr, pdir, &entry_point, &stack_addr);

   printk("[load_usermode_init] Entry: %p\n", entry_point);
   printk("[load_usermode_init] Stack: %p\n", stack_addr);

   usermode_init_task =
      create_first_usermode_task(pdir, entry_point, stack_addr);
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

   DEBUG_ONLY(bool tasklet_added =) add_tasklet0(&init_kb);
   ASSERT(tasklet_added);

   // kthread_create(&simple_test_kthread, (void*)0xAA1234BB);
   kmutex_test();
   kcond_test();

   kthread_create(&sleeping_kthread, (void *) 123);
   kthread_create(&sleeping_kthread, (void *) 20);
   kthread_create(&sleeping_kthread, (void *) (10*TIMER_HZ));
   //kthread_create(&tasklet_stress_test, NULL);

   load_usermode_init();

   printk("[kernel main] Starting the scheduler...\n");
   switch_to_idle_task_outside_interrupt_context();

   // We should never get here!
   NOT_REACHED();
}
