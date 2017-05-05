

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

   current_task = create_first_usermode_task(pdir, entry_point, stack_addr);
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
   printk("[kernel thread] This is a kernel thread..\n");

   for (int i = 0; i < 1024*(int)MB; i++) {
      if (!(i % (256*MB))) {
         printk("[kernel thread] i = %i\n", i/MB);
      }
   }
}

void tasklet_runner_kthread(void)
{
   printk("[kernel thread] tasklet runner kthread (pid: %i)\n",
          get_current_task()->pid);

   while (true) {

      bool res = run_one_tasklet();

      if (!res) {
         ASSERT(is_preemption_enabled());
         ASSERT(are_interrupts_enabled());

         //printk("[kernel thread] no tasklets, yield!\n");
         kernel_yield();

         //printk("after yield..\n");
      }
   }
}

kmutex test_mutex = { 0 };

void test_kmutex_thread1(void)
{
   printk("1) before lock\n");

   lock(&test_mutex);

   printk("1) under lock..\n");
   for (int i=0; i < 1024*1024*1024; i++) { }

   unlock(&test_mutex);

   printk("1) after lock\n");
}

void test_kmutex_thread2(void)
{
   printk("2) before lock\n");

   lock(&test_mutex);

   printk("2) under lock..\n");
   for (int i=0; i < 1024*1024*1024; i++) { }

   unlock(&test_mutex);

   printk("2) after lock\n");
}

void test_kmutex_thread3(void)
{
   printk("3) before trylock\n");

   bool locked = trylock(&test_mutex);

   if (locked) {

      printk("3) trylock SUCCEEDED: under lock..\n");

      if (locked) {
         unlock(&test_mutex);
      }

      printk("3) after lock\n");

   } else {
      printk("trylock returned FALSE\n");
   }
}


void kmutex_test(void)
{
   kmutex_init(&test_mutex);
   current_task = kthread_create(test_kmutex_thread1);
   current_task = kthread_create(test_kmutex_thread2);
   current_task = kthread_create(test_kmutex_thread3);
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
   initialize_tasklets();

   set_timer_freq(TIMER_HZ);

   irq_install_handler(X86_PC_TIMER_IRQ, timer_handler);
   irq_install_handler(X86_PC_KEYBOARD_IRQ, keyboard_handler);

   // TODO: make the kernel actually support the sysenter interface
   setup_sysenter_interface();

   mount_memdisk();
   //test_memdisk();

   disable_preemption();
   enable_interrupts();

   // Initialize the keyboard driver.
   init_kb();

   current_task = kthread_create(simple_test_kthread);
   current_task = kthread_create(tasklet_runner_kthread);

   kmutex_test();
   //load_usermode_init();
   schedule_outside_interrupt_context();

   // We should never get here!
   NOT_REACHED();
}
