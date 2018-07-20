
#include <exos/common/basic_defs.h>
#include <exos/common/string_util.h>
#include <exos/common/utils.h>
#include <exos/common/arch/generic_x86/cpu_features.h>

#include <multiboot.h>

#include <exos/kernel/hal.h>
#include <exos/kernel/irq.h>
#include <exos/kernel/kmalloc.h>
#include <exos/kernel/paging.h>
#include <exos/kernel/debug_utils.h>
#include <exos/kernel/process.h>
#include <exos/kernel/elf_loader.h>
#include <exos/kernel/tasklet.h>
#include <exos/kernel/sync.h>
#include <exos/kernel/fs/fat32.h>
#include <exos/kernel/fs/exvfs.h>
#include <exos/kernel/fs/devfs.h>
#include <exos/kernel/kb.h>
#include <exos/kernel/timer.h>
#include <exos/kernel/term.h>
#include <exos/kernel/pageframe_allocator.h>
#include <exos/kernel/datetime.h>
#include <exos/kernel/syscalls.h>
#include <exos/kernel/fb_console.h>
#include <exos/kernel/serial.h>
#include <exos/kernel/kb_scancode_set1_keys.h>
#include <exos/kernel/arch/generic_x86/textmode_video.h>
#include <exos/kernel/arch/generic_x86/fpu_memcpy.h>

extern uptr ramdisk_paddr;
extern size_t ramdisk_size;
extern u32 memsize_in_mb;

void save_multiboot_memory_map(multiboot_info_t *mbi);
void dump_system_memory_map(void);

/* Variables used by the cmdline parsing code */

extern void (*self_test_to_run)(void);
extern const char *const cmd_args[16];
void parse_kernel_cmdline(const char *cmdline);

/* -- */

void init_tty(void);

void read_multiboot_info(u32 magic, u32 mbi_addr)
{
   if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
      init_textmode_console(true);
      panic("The exOS kernel requires a multiboot-compatible bootloader.");
      return;
   }

   multiboot_info_t *mbi = (void *)(uptr)mbi_addr;
   memsize_in_mb = mbi->mem_upper/KB + 1;


   if (mbi->flags & MULTIBOOT_INFO_MODS) {
      if (mbi->mods_count >= 1) {
         multiboot_module_t *mod = ((multiboot_module_t *)(uptr)mbi->mods_addr);
         ramdisk_paddr = mod->mod_start;
         ramdisk_size = mod->mod_end - mod->mod_start;
      }
   }

   if (mbi->flags & MULTIBOOT_INFO_CMDLINE) {
      parse_kernel_cmdline((const char *)(uptr)mbi->cmdline);
   }

   if (mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO) {
      if (mbi->framebuffer_type != MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT) {
         set_framebuffer_info_from_mbi(mbi);
      }
   }

   if (mbi->flags & MULTIBOOT_INFO_MEM_MAP) {
      save_multiboot_memory_map(mbi);
   }
}

void show_hello_message(void)
{
   printk("Hello from exOS! [%s build, GCC %i.%i.%i]\n",
          BUILDTYPE_STR, __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
}

void show_system_info(void)
{
   printk("TIMER_HZ: %i; TIME_SLOT: %i ms; MEM: %i MB %s\n",
          TIMER_HZ,
          1000 / (TIMER_HZ / TIME_SLOT_JIFFIES),
          get_phys_mem_mb(),
          in_hypervisor() ? "[IN HYPERVISOR]" : "");

   dump_system_memory_map();
}


void mount_ramdisk(void)
{
   if (!ramdisk_size) {
      printk("[WARNING] No RAMDISK found.\n");
      return;
   }

   filesystem *root_fs =
      fat_mount_ramdisk(KERNEL_PA_TO_VA(ramdisk_paddr), EXVFS_FS_RO);

   if (!root_fs)
      panic("Unable to mount the fat32 RAMDISK");

   int rc = mountpoint_add(root_fs, "/");

   if (rc != 0)
      panic("mountpoint_add() failed with error: %d", rc);

   printk("Mounted RAMDISK at PADDR %p.\n", ramdisk_paddr);
}

void selftest_runner_thread()
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
         debug_show_spurious_irq_count();
         return KB_HANDLER_OK_AND_STOP;

      case KEY_F2:
         debug_kmalloc_dump_mem_usage();
         return KB_HANDLER_OK_AND_STOP;

      case KEY_F3:
         debug_term_print_scroll_cycles();
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
}

void kmain(u32 multiboot_magic, u32 mbi_addr)
{
   init_serial_port();
   create_kernel_process();
   setup_soft_interrupt_handling();
   read_multiboot_info(multiboot_magic, mbi_addr);

   show_hello_message();

   get_x86_cpu_features();
   enable_cpu_features();
   init_fpu_memcpy();

   setup_segmentation();

   init_paging();
   init_kmalloc();
   init_paging_cow();
   init_pageframe_allocator(); /* NOTE: unused at the moment */

   init_console();

   //debug_kmalloc_dump_mem_usage();

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

      if (!kthread_create(selftest_runner_thread, NULL))
         panic("Unable to create the selftest_runner_thread");

      switch_to_idle_task_outside_interrupt_context();
   }

   if (!ramdisk_size) {
      panic("No ramdisk and no selftest requested: nothing to do.");
   }

   enable_preemption();
   push_nested_interrupt(-1);
   printk("[main] sys_execve('%s')\n", cmd_args[0]);
   sptr rc = sys_execve(cmd_args[0], cmd_args, NULL);
   panic("execve('%s') failed with %i\n", cmd_args[0], rc);
}
