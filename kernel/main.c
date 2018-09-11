
#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/utils.h>
#include <tilck/common/arch/generic_x86/cpu_features.h>

#include <multiboot.h>

#include <tilck/kernel/hal.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/elf_loader.h>
#include <tilck/kernel/tasklet.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/fs/fat32.h>
#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/fs/devfs.h>
#include <tilck/kernel/kb.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/datetime.h>
#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/fb_console.h>
#include <tilck/kernel/serial.h>
#include <tilck/kernel/kb_scancode_set1_keys.h>
#include <tilck/kernel/arch/generic_x86/textmode_video.h>
#include <tilck/kernel/arch/generic_x86/fpu_memcpy.h>
#include <tilck/kernel/system_mmap.h>


/* Variables used by the cmdline parsing code */

extern void (*self_test_to_run)(void);
extern const char *const cmd_args[16];
void parse_kernel_cmdline(const char *cmdline);

/* -- */

void init_tty(void);
void show_banner(void);

void read_multiboot_info(u32 magic, u32 mbi_addr)
{
   multiboot_info_t *mbi = (void *)(uptr)mbi_addr;

   if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
      panic("The Tilck kernel requires a multiboot-compatible bootloader");

   if (!(mbi->flags & MULTIBOOT_INFO_MEM_MAP))
      panic("No memory map in the multiboot info struct");

   if (mbi->flags & MULTIBOOT_INFO_MODS) {

      multiboot_module_t *mods = (void *)(uptr)mbi->mods_addr;

      for (u32 i = 0; i < mbi->mods_count; i++)
         system_mmap_add_ramdisk(mods[i].mod_start, mods[i].mod_end);
   }

   if (mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO) {
      if (mbi->framebuffer_type != MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT) {
         set_framebuffer_info_from_mbi(mbi);
      }
   }

   if (!(mbi->flags & MULTIBOOT_INFO_MEM_MAP))
      panic("Tilck requires the bootloader to provide a full memory map");

   system_mmap_set(mbi);

   if (mbi->flags & MULTIBOOT_INFO_CMDLINE)
      parse_kernel_cmdline((const char *)(uptr)mbi->cmdline);
}

void show_hello_message(void)
{
   printk("Hello from Tilck! [%s build, GCC %i.%i.%i]\n",
          BUILDTYPE_STR, __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
}

void show_system_info(void)
{
   printk("TIMER_HZ: %i; TIME_SLOT: %i ms %s\n",
          TIMER_HZ,
          1000 / (TIMER_HZ / TIME_SLOT_TICKS),
          in_hypervisor() ? "[IN HYPERVISOR]" : "");

   show_banner();
}

void mount_ramdisk(void)
{
   void *ramdisk = system_mmap_get_ramdisk_vaddr(0);

   if (!ramdisk) {
      printk("[WARNING] No RAMDISK found.\n");
      return;
   }

   filesystem *root_fs =
      fat_mount_ramdisk(ramdisk, VFS_FS_RO);

   if (!root_fs)
      panic("Unable to mount the fat32 RAMDISK");

   int rc = mountpoint_add(root_fs, "/");

   if (rc != 0)
      panic("mountpoint_add() failed with error: %d", rc);
}

void se_runner_thread()
{
   self_test_to_run();
}

void init_console(void)
{
   if (use_framebuffer())
      init_framebuffer_console(in_hypervisor());
   else
      init_textmode_console(in_hypervisor());
}

int debug_f_key_press_handler(u32 key, u8 c)
{
   switch (key) {

      case KEY_F1:
         debug_show_build_opts();
         return KB_HANDLER_OK_AND_STOP;

      case KEY_F2:
         debug_kmalloc_dump_mem_usage();
         return KB_HANDLER_OK_AND_STOP;

      case KEY_F3:
         dump_system_memory_map();
         return KB_HANDLER_OK_AND_STOP;

      case KEY_F4:
         debug_term_print_scroll_cycles();
         return KB_HANDLER_OK_AND_STOP;

      case KEY_F5:
         debug_show_spurious_irq_count();
         return KB_HANDLER_OK_AND_STOP;

      default:
         return KB_HANDLER_NAK;
   }
}

void init_drivers(void)
{
   init_kb();

   if (kb_register_keypress_handler(&debug_f_key_press_handler) < 0)
      panic("Unable to register debug Fn keypress handler");

   init_tty();

   if (use_framebuffer())
      init_fbdev();
}

void kmain(u32 multiboot_magic, u32 mbi_addr)
{
   init_serial_port();
   create_kernel_process();
   setup_soft_interrupt_handling();
   read_multiboot_info(multiboot_magic, mbi_addr);

   get_x86_cpu_features();
   enable_cpu_features();
   init_fpu_memcpy();

   setup_segmentation();

   init_paging();
   init_kmalloc();
   init_paging_cow();

   init_console();
   show_hello_message();

   setup_irq_handling();
   init_sched();
   setup_syscall_interfaces();

   show_system_info();

   init_tasklets();
   init_timer();

   mount_ramdisk();
   create_and_register_devfs();

   if (!enqueue_tasklet0(0, &init_drivers))
      panic("Unable to enqueue a tasklet for init_drivers()");

   if (self_test_to_run) {

      if (!kthread_create(se_runner_thread, NULL))
         panic("Unable to create the se_runner_thread");

      switch_to_idle_task_outside_interrupt_context();
   }

   if (!system_mmap_get_ramdisk_vaddr(0)) {
      panic("No ramdisk and no selftest requested: nothing to do.");
   }

   enable_preemption();
   push_nested_interrupt(-1);
   sptr rc = sys_execve(cmd_args[0], cmd_args, NULL);
   panic("execve('%s') failed with %i\n", cmd_args[0], rc);
}
