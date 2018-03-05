

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

extern u32 memsize_in_mb;
extern uptr ramdisk_paddr;
extern size_t ramdisk_size;
extern task_info *usermode_init_task;

void dump_multiboot_info(multiboot_info_t *mbi)
{
   printk("MBI ptr: %p [+ %u KB]\n", mbi, ((uptr)mbi)/KB);
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
}

void show_hello_message(void)
{
   printk("Hello from exOS! [%s build]\n", BUILDTYPE_STR);
}

void read_multiboot_info(u32 magic, u32 mbi_addr)
{
   if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
      return;

   multiboot_info_t *mbi = (void *)(uptr)mbi_addr;
   memsize_in_mb = (mbi->mem_upper)/1024 + 1;

   printk("*** Detected multiboot ***\n");

   if (mbi->flags & MULTIBOOT_INFO_MODS) {
      if (mbi->mods_count >= 1) {

         multiboot_module_t *mod = ((multiboot_module_t *)(uptr)mbi->mods_addr);
         ramdisk_paddr = mod->mod_start;
         ramdisk_size = mod->mod_end - mod->mod_start;

      } else {
         ramdisk_paddr = 0;
         ramdisk_size = 0;
      }
   }

}

void show_additional_info(void)
{
   printk("TIMER_HZ: %i; MEM: %i MB\n",
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

char hack_buf1[16*1024] __attribute__ ((section (".symtab2"))) = {0};
char hack_buf2[16*1024] __attribute__ ((section (".strtab2"))) = {0};

void mount_ramdisk(void)
{
   if (!ramdisk_size) {
      printk("[WARNING] No RAMDISK found.\n");
      return;
   }

   printk("Mounting RAMDISK at PADDR %p...\n", ramdisk_paddr);
   filesystem *root_fs = fat_mount_ramdisk(KERNEL_PA_TO_VA(ramdisk_paddr));
   mountpoint_add(root_fs, "/");
}

void dump_kernel_symtab(void);

void kmain(u32 multiboot_magic, u32 mbi_addr)
{
   term_init();
   show_hello_message();
   read_multiboot_info(multiboot_magic, mbi_addr);
   show_additional_info();

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

   mount_ramdisk();

   ///////////////////////////////////////////
   // DEBUG STUFF

   dump_kernel_symtab();
   /////////////////////////////////////////

   //kthread_create(&simple_test_kthread, (void*)0xAA1234BB);
   //kmutex_test();
   //kcond_test();

   //kernel_kmalloc_perf_test();

   //kthread_create(&sleeping_kthread, (void *) 123);
   //kthread_create(&sleeping_kthread, (void *) 20);
   //kthread_create(&sleeping_kthread, (void *) (10*TIMER_HZ));
   //kthread_create(&tasklet_stress_test, NULL);

   //kernel_alloc_pageframe_perftest();

   //if (ramdisk_size)
   //   load_usermode_init();

   printk("[kernel main] Starting the scheduler...\n");
   switch_to_idle_task_outside_interrupt_context();

   // We should never get here!
   NOT_REACHED();
}
