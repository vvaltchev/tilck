/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/utils.h>
#include <tilck/common/arch/generic_x86/cpu_features.h>

#include <multiboot.h>

#include <tilck/kernel/hal.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/elf_loader.h>
#include <tilck/kernel/tasklet.h>
#include <tilck/kernel/fs/fat32.h>
#include <tilck/kernel/fs/devfs.h>
#include <tilck/kernel/kb.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/fb_console.h>
#include <tilck/kernel/serial.h>
#include <tilck/kernel/arch/generic_x86/textmode_video.h>
#include <tilck/kernel/system_mmap.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/cmdline.h>
#include <tilck/kernel/self_tests.h>
#include <tilck/kernel/tty.h>
#include <tilck/kernel/term.h>

void init_console(void)
{
   if (use_framebuffer())
      init_framebuffer_console();
   else
      init_textmode_console();

   printk_flush_ringbuf();
}

static void read_multiboot_info(u32 magic, u32 mbi_addr)
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

static void show_hello_message(void)
{
#ifndef __clang__
   printk("Hello from Tilck! [%s build, GCC %i.%i.%i]\n", BUILDTYPE_STR,
          __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#else
   printk("Hello from Tilck! [%s build, Clang %i.%i.%i]\n", BUILDTYPE_STR,
          __clang_major__, __clang_minor__, __clang_patchlevel__);
#endif
}

static void show_system_info(void)
{
   void show_banner(void);

   printk("TIMER_HZ: %i; TIME_SLOT: %i ms %s\n",
          TIMER_HZ,
          1000 / (TIMER_HZ / TIME_SLOT_TICKS),
          in_hypervisor() ? "[IN HYPERVISOR]" : "");

   show_banner();
}

static void mount_first_ramdisk(void)
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


static void init_drivers(void)
{
   init_kb();
   register_debug_kernel_keypress_handler();
   init_tty();
   show_system_info();

   if (use_framebuffer())
      init_fbdev();

   init_serial_comm();
}

static void async_init_drivers(void)
{
   if (!enqueue_tasklet0(0, &init_drivers))
      panic("Unable to enqueue a tasklet for init_drivers()");
}

void kmain(u32 multiboot_magic, u32 mbi_addr)
{
   call_kernel_global_ctors();

   early_init_serial_ports();
   create_kernel_process();
   setup_soft_interrupt_handling();
   read_multiboot_info(multiboot_magic, mbi_addr);

   get_cpu_features();
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

   //show_system_info();

   init_tasklets();
   init_timer();

   mount_first_ramdisk();
   create_and_register_devfs();

   async_init_drivers();

   if (self_test_to_run) {
      kernel_run_selected_selftest();
      NOT_REACHED();
   }

   if (!system_mmap_get_ramdisk_vaddr(0)) {
      panic("No ramdisk and no selftest requested: nothing to do.");
   }

   enable_preemption();
   push_nested_interrupt(-1);
   sptr rc = sys_execve(cmd_args[0], cmd_args, NULL);
   panic("execve('%s') failed with %i\n", cmd_args[0], rc);
}
