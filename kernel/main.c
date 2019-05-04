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
#include <tilck/kernel/process.h>

void init_console(void)
{
   if (!kopt_serial_console) {

      if (use_framebuffer())
         init_framebuffer_console();
      else
         init_textmode_console();

   } else {

      init_term(get_curr_term(), NULL, 25, 80, COM1);
   }

   printk_flush_ringbuf();
}

static void read_multiboot_info(u32 magic, u32 mbi_addr)
{
   multiboot_info_t *mbi = TO_PTR(mbi_addr);

   if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
      panic("The Tilck kernel requires a multiboot-compatible bootloader");

   if (!(mbi->flags & MULTIBOOT_INFO_MEM_MAP))
      panic("No memory map in the multiboot info struct");

   if (mbi->flags & MULTIBOOT_INFO_MODS) {

      multiboot_module_t *mods = TO_PTR(mbi->mods_addr);

      for (u32 i = 0; i < mbi->mods_count; i++)
         system_mmap_add_ramdisk(mods[i].mod_start, mods[i].mod_end);
   }

   if (mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO)
      if (mbi->framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_RGB)
         set_framebuffer_info_from_mbi(mbi);

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
   /* declare the ramfs_create() function */
   filesystem *ramfs_create(void);

   void *ramdisk = system_mmap_get_ramdisk_vaddr(0);
   filesystem *initrd, *ramfs;
   int rc;

   if (!ramdisk) {
      printk("[WARNING] No RAMDISK found.\n");
      return;
   }

   initrd = fat_mount_ramdisk(ramdisk, VFS_FS_RO);

   if (!initrd)
      panic("Unable to mount the initrd fat32 RAMDISK");

   rc = mountpoint_add(initrd, "/");

   if (rc != 0)
      panic("mountpoint_add() failed with error: %d", rc);

   /* -------------------------------------------- */
   /* mount the ramdisk at /tmp                    */

   ramfs = ramfs_create();

   if (!ramfs)
      panic("Unable to create ramfs");

   rc = mountpoint_add(ramfs, "/tmp/");

   if (rc != 0)
      panic("mountpoint_add() failed with error: %d", rc);
}

static void run_init_or_selftest(void)
{
   if (self_test_to_run) {

      if (KERNEL_SELFTESTS) {
         self_test_to_run();
         return;
      }

      panic("The kernel was not compiled with self-tests");

   } else {

      if (!system_mmap_get_ramdisk_vaddr(0))
         panic("No ramdisk and no selftest requested: nothing to do.");

      /* Run /sbin/init or whatever program was passed in the cmdline */
      sptr rc = first_execve(cmd_args[0], cmd_args);

      if (rc != 0)
         panic("execve('%s') failed with %i\n", cmd_args[0], rc);
   }
}

static void do_async_init()
{
   mount_first_ramdisk();
   init_devfs();

   if (!kopt_serial_console) {
      init_kb();
      register_debug_kernel_keypress_handler();
   }

   init_tty();
   init_fbdev();
   init_serial_comm();

   show_hello_message();
   show_system_info();
   run_init_or_selftest();
}

static void async_init(void)
{
   if (kthread_create(&do_async_init, NULL) < 0)
      panic("Unable to create a kthread for do_async_init()");
}

void kmain(u32 multiboot_magic, u32 mbi_addr)
{
   call_kernel_global_ctors();
   early_init_serial_ports();
   create_kernel_process();
   init_cpu_exception_handling();
   read_multiboot_info(multiboot_magic, mbi_addr);

   enable_cpu_features();
   init_fpu_memcpy();

   init_segmentation();
   init_paging();
   init_kmalloc();
   init_paging_cow();
   init_console();
   init_irq_handling();
   init_sched();
   init_syscall_interfaces();
   init_tasklets();
   init_timer();
   init_system_clock();

   async_init();
   schedule_outside_interrupt_context();
}
