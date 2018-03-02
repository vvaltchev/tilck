

#include <common_defs.h>
#include <string_util.h>
#include <hal.h>

#include <irq.h>
#include <kmalloc.h>
#include <paging.h>
#include <debug_utils.h>
#include <process.h>
#include <elf_loader.h>

#include <utils.h>
#include <tasklet.h>
#include <sync.h>

#include <fs/fat32.h>
#include <fs/exvfs.h>

#include <kb.h>
#include <timer.h>
#include <term.h>
#include <pageframe_allocator.h>
#include <multiboot.h>

#include <self_tests/self_tests.h>

task_info *usermode_init_task = NULL;

extern u32 memsize_in_mb;

void show_hello_message(u32 magic, u32 mbi_addr)
{
   printk("Hello from exOS! [%s build]\n", BUILDTYPE_STR);

   if (magic == MULTIBOOT_BOOTLOADER_MAGIC) {

      struct multiboot_info *mbi = (void *)(uptr)mbi_addr;

      printk("*** Detected multiboot 1 magic ***\n");

      printk("Mbi ptr: %p\n", mbi);
      printk("mem lower: %u KB\n", mbi->mem_lower + 1);
      printk("mem upper: %u MB\n", (mbi->mem_upper)/1024 + 1);

      if (mbi->flags & MULTIBOOT_INFO_CMDLINE) {
         printk("Cmdline: '%s'\n", (char *)(uptr)mbi->cmdline);
      }

      if (mbi->flags & MULTIBOOT_INFO_VBE_INFO) {
         printk("VBE mode: %p\n", mbi->vbe_mode);
      }

      if (mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO) {
         printk("Framebuffer addr: %p\n", mbi->framebuffer_addr);
      }

      if (mbi->flags & MULTIBOOT_INFO_MODS) {

         printk("Mods count: %u\n", mbi->mods_count);

         for (u32 i = 0; i < mbi->mods_count; i++) {

            multiboot_module_t *mod =
               ((multiboot_module_t *)(uptr)mbi->mods_addr)+i;

            printk("mod cmdline: '%s'\n", mod->cmdline);
            printk("mod start: %p [+ %u KB]\n", mod->mod_start,
                                                mod->mod_start/KB);
            printk("mod end:   %p [+ %u KB]\n", mod->mod_end,
                                                mod->mod_end/KB);
            printk("mod size:  %u KB\n", (mod->mod_end-mod->mod_start)/KB);
         }

      }

      if (mbi->flags & MULTIBOOT_INFO_ELF_SHDR) {
         printk("ELF section table available\n");
         printk("num:   %u\n", mbi->u.elf_sec.num);
         printk("addr:  %p\n", mbi->u.elf_sec.addr);
         printk("size:  %u\n", mbi->u.elf_sec.size);
         printk("shndx: %p\n", mbi->u.elf_sec.shndx);
      }

      memsize_in_mb = (mbi->mem_upper)/1024 + 1;
   }

   printk("TIMER_HZ: %i; Supported memory: %i MB\n",
           TIMER_HZ, get_phys_mem_mb());
}

void load_usermode_init()
{
   void *entry_point = NULL;
   void *stack_addr = NULL;
   page_directory_t *pdir = NULL;

   load_elf_program("/sbin/init", &pdir, &entry_point, &stack_addr);

   usermode_init_task =
      create_first_usermode_task(pdir, entry_point, stack_addr);

   printk("[load_usermode_init] Entry: %p\n", entry_point);
   printk("[load_usermode_init] Stack: %p\n", stack_addr);
}


void mount_ramdisk(void)
{
   filesystem *root_fs = fat_mount_ramdisk(KERNEL_PA_TO_VA(RAMDISK_PADDR));
   mountpoint_add(root_fs, "/");
}

void kmain(u32 multiboot_magic, u32 mbi_addr)
{
   term_init();
   show_hello_message(multiboot_magic, mbi_addr);

   setup_segmentation();
   setup_interrupt_handling();

   init_pageframe_allocator();

   init_paging();
   initialize_kmalloc();
   init_paging_cow();

   initialize_scheduler();
   initialize_tasklets();

   set_timer_freq(TIMER_HZ);

   irq_install_handler(X86_PC_TIMER_IRQ, timer_handler);
   irq_install_handler(X86_PC_KEYBOARD_IRQ, keyboard_handler);

   DEBUG_ONLY(bool tasklet_added =) add_tasklet0(&init_kb);
   ASSERT(tasklet_added);

   // TODO: make the kernel actually support the sysenter interface
   setup_sysenter_interface();

   //mount_ramdisk();

   //kthread_create(&simple_test_kthread, (void*)0xAA1234BB);
   //kmutex_test();
   //kcond_test();

   //kernel_kmalloc_perf_test();

   //kthread_create(&sleeping_kthread, (void *) 123);
   //kthread_create(&sleeping_kthread, (void *) 20);
   //kthread_create(&sleeping_kthread, (void *) (10*TIMER_HZ));
   //kthread_create(&tasklet_stress_test, NULL);

   //kernel_alloc_pageframe_perftest();

   //load_usermode_init();

   printk("[kernel main] Starting the scheduler...\n");
   switch_to_idle_task_outside_interrupt_context();

   // We should never get here!
   NOT_REACHED();
}
