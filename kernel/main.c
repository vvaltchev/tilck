

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

void run_usermode_init()
{
   void *elf_vaddr = (void *) (RAM_DISK_VADDR + INIT_PROGRAM_MEM_DISK_OFFSET);

   page_directory_t *pdir = pdir_clone(get_kernel_page_dir());
   set_page_directory(pdir);

   void *entry_point = NULL;
   void *stack_addr = NULL;
   load_elf_program(elf_vaddr, pdir, &entry_point, &stack_addr);

   printk("[run_usermode_init] Entry: %p\n", entry_point);
   printk("[run_usermode_init] Stack: %p\n", stack_addr);

   first_usermode_switch(pdir, entry_point, stack_addr);
}

void show_hello_message()
{
   printk("Hello from my kernel!\n");
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

void test_memdisk()
{
   char *ptr;

   printk("Data at %p:\n", 0x0);
   ptr = (char *)RAM_DISK_VADDR;
   for (int i = 0; i < 16; i++) {
      printk("%x ", (u8)ptr[i]);
   }
   printk("\n");

   printk("Data at %p:\n", INIT_PROGRAM_MEM_DISK_OFFSET);
   ptr = (char *)(RAM_DISK_VADDR + INIT_PROGRAM_MEM_DISK_OFFSET);
   for (int i = 0; i < 16; i++) {
      printk("%x ", (u8)ptr[i]);
   }
   printk("\n");



   printk("\n\n");
   printk("Calculating CRC32...\n");
   u32 crc = crc32(0, (void *)RAM_DISK_VADDR, RAM_DISK_SIZE);
   printk("Crc32 of the data: %p\n", crc);
}


void simple_test_kthread(void)
{
   disable_preemption();
   {
      printk("[kernel thread] This is a kernel thread..\n");
   }
   enable_preemption();

   for (int i = 0; i < 1024*(int)MB; i++) {
      if (!(i % (256*MB))) {

         disable_preemption();
         {
            printk("[kernel thread] i = %i\n", i/MB);
         }
         enable_preemption();
      }
   }
}

void tasklet_runner_kthread(void)
{
   bool res;

   disable_preemption();
   {
      printk("[kernel thread] tasklet runner kthread (pid: %i)\n",
             get_current_task()->pid);
   }
   enable_preemption();

   while (true) {

      res = run_one_tasklet();

      if (!res) {
         ASSERT(is_preemption_enabled());
         ASSERT(are_interrupts_enabled());

         halt();
      }
   }
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

   //irq_set_mask(X86_PC_TIMER_IRQ);

   irq_install_handler(X86_PC_TIMER_IRQ, timer_handler);
   irq_install_handler(X86_PC_KEYBOARD_IRQ, keyboard_handler);

   setup_sysenter_interface();

   mount_memdisk();
   //test_memdisk();

   initialize_tasklets();

   enable_interrupts();

   // Initialize the keyboard driver.
   init_kb();

   kthread_create(simple_test_kthread);
   //kthread_create(tasklet_runner_kthread);

   // Run the 'init' usermode program.
   run_usermode_init();

   //while(1) halt();

   // We should never get here!
   NOT_REACHED();
}
