/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_modules.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/utils.h>

#include <multiboot.h>

#include <tilck/kernel/modules.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/elf_loader.h>
#include <tilck/kernel/tasklet.h>
#include <tilck/kernel/fs/fat32.h>
#include <tilck/kernel/fs/devfs.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/system_mmap.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/cmdline.h>
#include <tilck/kernel/self_tests.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/fs/kernelfs.h>

#include <tilck/mods/fb_console.h>
#include <tilck/mods/serial.h>

void init_console(void)
{
   if (kopt_serial_console) {

      if (!serial_term_intf) {

         if (!video_term_intf)
            goto we_are_doomed;

         kopt_serial_console = false;
         panic("Unable to init the serial console without the serial module!");
      }

      __curr_term_intf = serial_term_intf;
      __curr_term = __curr_term_intf->get_first_term();

      init_curr_term(NULL, 25, 80, COM1, 0);
      printk_flush_ringbuf();
      return;
   }

   if (!video_term_intf) {

      if (!serial_term_intf)
         goto we_are_doomed;

      kopt_serial_console = true;
      panic("Unable to init the video console without the console module!");
   }

   __curr_term_intf = video_term_intf;
   __curr_term = __curr_term_intf->get_first_term();

   if (use_framebuffer()) {

      if (MOD_fb)
         init_fb_console();
      else
         init_curr_term(NULL, 25, 80, 0, 0); /* no-output term */

   } else {
      init_textmode_console();
   }

   printk_flush_ringbuf();
   return;

we_are_doomed:
   disable_interrupts_forced();

   while (1)
      halt();
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

   if (MOD_fb) {
      if (mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO)
         if (mbi->framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_RGB)
            set_framebuffer_info_from_mbi(mbi);
   }

   if (!(mbi->flags & MULTIBOOT_INFO_MEM_MAP))
      panic("Tilck requires the bootloader to provide a full memory map");

   system_mmap_set(mbi);

   if (mbi->flags & MULTIBOOT_INFO_CMDLINE)
      parse_kernel_cmdline((const char *)(uptr)mbi->cmdline);
}

static void show_hello_message(void)
{
   printk("Hello from Tilck! [%s build, %s %i.%i.%i]\n", BUILDTYPE_STR,
          COMPILER_NAME, __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
}

static void show_system_info(void)
{
   void show_tilck_logo(void);

   printk("TIMER_HZ: %i; TIME_SLOT: %i ms %s\n",
          TIMER_HZ,
          1000 / (TIMER_HZ / TIME_SLOT_TICKS),
          in_hypervisor() ? "[IN HYPERVISOR]" : "");

   if (KERNEL_SHOW_LOGO)
      show_tilck_logo();
}

static void mount_initrd(void)
{
   /* declare the ramfs_create() function */
   struct fs *ramfs_create(void);

   void *ramdisk = system_mmap_get_ramdisk_vaddr(0);
   struct fs *initrd, *ramfs;
   int rc;

   if (!(ramfs = ramfs_create()))
      panic("Unable to create ramfs");

   if ((rc = mp_init(ramfs)))
      panic("mp_init() failed with error: %d", rc);

   if ((rc = vfs_mkdir("/dev", 0777)))
      panic("vfs_mkdir(\"/dev\") failed with error: %d", rc);

   if ((rc = vfs_mkdir("/tmp", 0777)))
      panic("vfs_mkdir(\"/tmp\") failed with error: %d", rc);

   /* Set kernel's process `cwd` to the root folder */
   {
      struct vfs_path tp;
      struct process *pi = kernel_process_pi;
      ASSERT(pi == get_curr_task()->pi);

      tp.fs = mp_get_root();
      vfs_get_root_entry(tp.fs, &tp.fs_path);
      process_set_cwd2_nolock_raw(pi, &tp);
   }

   if (LIKELY(ramdisk != NULL)) {

      if (!(initrd = fat_mount_ramdisk(ramdisk, VFS_FS_RO)))
         panic("Unable to mount the initrd fat32 RAMDISK");

      if ((rc = vfs_mkdir("/initrd", 0777)))
         panic("vfs_mkdir(\"/initrd\") failed with error: %d", rc);

      if ((rc = mp_add(initrd, "/initrd")))
         panic("mp_add() failed with error: %d", rc);

   } else {

      printk("[WARNING] No RAMDISK found.\n");
   }
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

      /* Run init or whatever program was passed in the cmdline */
      sptr rc = first_execve(cmd_args[0], cmd_args);

      if (rc != 0)
         panic("execve('%s') failed with %i\n", cmd_args[0], rc);
   }
}

static void do_async_init()
{
   mount_initrd();
   init_devfs();
   init_modules();
   init_extra_debug_features();

   show_hello_message();
   show_system_info();
   run_init_or_selftest();
}

static void async_init(void)
{
   if (kthread_create(&do_async_init, KTH_ALLOC_BUFS, NULL) < 0)
      panic("Unable to create a kthread for do_async_init()");
}

void kmain(u32 multiboot_magic, u32 mbi_addr)
{
   call_kernel_global_ctors();

   if (MOD_serial)
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
   init_system_time();
   init_kernelfs();

   async_init();
   schedule_outside_interrupt_context();
}
