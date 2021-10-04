/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_debug.h>
#include <tilck_gen_headers/mod_console.h>
#include <tilck_gen_headers/mod_fb.h>
#include <tilck_gen_headers/mod_acpi.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>

#include <multiboot.h>

#include <tilck/kernel/modules.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/elf_loader.h>
#include <tilck/kernel/worker_thread.h>
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
#include <tilck/kernel/fs/vfs.h>

#include <tilck/mods/console.h>
#include <tilck/mods/fb_console.h>
#include <tilck/mods/serial.h>
#include <tilck/mods/acpi.h>

#include <3rd_party/acpi/acpi.h>
#include <3rd_party/acpi/acexcep.h>

static bool read_multiboot_info_passed;
static u32 saved_multiboot_magic;
static multiboot_info_t *saved_multiboot_mbi;
static void read_multiboot_info(void);

static void
save_multiboot_info(u32 magic, u32 mbi)
{
   saved_multiboot_magic = magic;
   saved_multiboot_mbi = TO_PTR(mbi);
}

static void
init_video_console(void)
{
   if (in_panic()) {

      /*
       * We're in panic and there's no video console yet. Well, we can
       * initialize it, but we have to check if we at least read the multiboot
       * info first.
       */

      if (!read_multiboot_info_passed)
         read_multiboot_info();
   }

   if (!use_framebuffer()) {

      if (MOD_console) {

         /*
          * NOTE: since we got here, we're sure that we have the console module
          * built-in, but we still have to put the call under an if, in order
          * to remove the link dependency on that symbol in case the console
          * module is _not_ built-in.
          */

         if (MOD_acpi &&
             get_acpi_init_status() >= ais_tables_initialized &&
             acpi_is_vga_text_mode_avail() == tri_no)
         {

            /* no text mode on this machine */
            init_first_term_null();

         } else {

            /*
             * No ACPI or ACPI tables are not initialized (panic?) or
             * vga text mode is available or ACPI's FADT is too old to tell
             * whether text mode is available or not so we assume it is.
             */
            init_textmode_console();
         }
      }

      return;
   }

   /* framebuffer console */

   if (!MOD_fb) {

      /*
       * This compile-time check (MOD_fb will evalute as 0 or 1) instead, is
       * strictly necessary, as the kernel could be loaded in graphical mode
       * without having the `fb` module built-in. In this case, we just won't
       * display anything on the screen.
       */
      init_first_term_null();
      return;
   }

   init_fb_console();
}

void
init_console(void)
{
   printk("*** Init the kernel console\n");

   if (kopt_serial_console) {

      if (!serial_term_intf) {

         /*
          * We have to use the serial console, but the module is not built-in:
          * disable it and display an error on video, if we can. Otherwise,
          * we're simply doomed.
          */

         if (!video_term_intf)
            goto we_are_doomed;

         kopt_serial_console = false;
         panic("Unable to init the serial console without the serial module!");
      }

      init_first_serial_term(COM1);
      printk_flush_ringbuf();
      return;
   }

   if (!video_term_intf) {

      /*
       * We have to use the video console, but the module is not built-in:
       * disable it and send an error on the serial port, if we can. Otherwise,
       * we're simply doomed.
       */

      if (!serial_term_intf)
         goto we_are_doomed;

      kopt_serial_console = true;
      panic("Unable to init the video console without the console module!");
   }

   init_video_console();
   printk_flush_ringbuf();
   return;

we_are_doomed:
   disable_interrupts_forced();

   while (1)
      halt();
}

static void
read_multiboot_info(void)
{
   multiboot_info_t *mbi = saved_multiboot_mbi;

   if (read_multiboot_info_passed)
      return;             /* We're probably in panic: just do nothing */

   if (saved_multiboot_magic != MULTIBOOT_BOOTLOADER_MAGIC) {

      if (in_panic())
         goto out;        /* We're already in panic: just fail silently */

      panic("The Tilck kernel requires a multiboot-compatible bootloader");
   }

   if (~mbi->flags & MULTIBOOT_INFO_MEM_MAP) {

      if (in_panic())
         goto out;        /* We're already in panic: just fail silently */

      panic("No memory map in the multiboot info struct");
   }

   if (mbi->flags & MULTIBOOT_INFO_BOOT_LOADER_NAME) {

      const char *name = TO_PTR(mbi->boot_loader_name);

      if (!strcmp(name, "TILCK_EFI")) {

         printk("Multiboot: detected the TILCK_EFI bootloader\n");
         printk("Multiboot: ACPI root ptr: %p\n", TO_PTR(mbi->apm_table));
         acpi_set_root_pointer(mbi->apm_table);
      }
   }

   /* Loading ramdisk(s) is not even worth considering if we're in panic */
   if (!in_panic()) {

      if (mbi->flags & MULTIBOOT_INFO_MODS) {

         multiboot_module_t *mods = TO_PTR(mbi->mods_addr);

         for (u32 i = 0; i < mbi->mods_count; i++)
            system_mmap_add_ramdisk(mods[i].mod_start, mods[i].mod_end);
      }
   }

   /* Framebuffer info is essential, even if we're in panic */
   if (MOD_fb) {
      if (mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO)
         if (mbi->framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_RGB)
            set_framebuffer_info_from_mbi(mbi);
   }

   /* Set system's memory map based on what was provided to us via multiboot */
   system_mmap_set(mbi);

   /* It's safer to stick to the default settings, when we're in panic */
   if (!in_panic()) {
      if (mbi->flags & MULTIBOOT_INFO_CMDLINE)
         parse_kernel_cmdline(TO_PTR(mbi->cmdline));
   }

out:
   read_multiboot_info_passed = true;

   /* Reset the temporary multiboot variables as they won't be used again */
   saved_multiboot_magic = 0;
   saved_multiboot_mbi = NULL;
}

static void
mount_initrd(void)
{
   /* declare the ramfs_create() function */
   struct fs *ramfs_create(void);

   struct fs *initrd, *ramfs;
   void *ramdisk;
   size_t ramdisk_size;
   int rc;

   if (system_mmap_get_ramdisk(0, &ramdisk, &ramdisk_size) < 0)
      panic("system_mmap_get_ramdisk_vaddr(0) failed");

   if (!(ramfs = ramfs_create()))
      panic("Unable to create ramfs");

   if ((rc = mp_init(ramfs)))
      panic("mp_init() failed with error: %d", rc);

   if ((rc = vfs_mkdir("/tmp", 0777)))
      panic("vfs_mkdir(\"/tmp\") failed with error: %d", rc);

   /* Set kernel's process `cwd` to the root folder */
   {
      struct vfs_path tp;
      struct process *pi = kernel_process_pi;
      ASSERT(pi == get_curr_proc());

      tp.fs = mp_get_root();
      vfs_get_root_entry(tp.fs, &tp.fs_path);
      process_set_cwd2_nolock_raw(pi, &tp);
   }

   if (LIKELY(ramdisk != NULL)) {

      if (!(initrd = fat_mount_ramdisk(ramdisk, ramdisk_size, 0)))
         panic("Unable to mount the initrd fat32 RAMDISK");

      if ((rc = vfs_mkdir("/initrd", 0777)))
         panic("vfs_mkdir(\"/initrd\") failed with error: %d", rc);

      if ((rc = mp_add(initrd, "/initrd")))
         panic("mp_add() failed with error: %d", rc);

   } else {

      printk("[WARNING] No RAMDISK found.\n");
   }
}

static void
run_init_or_selftest(void)
{
   if (self_test_to_run) {

      if (KERNEL_SELFTESTS) {
         self_test_to_run();
         return;
      }

      panic("The kernel was not compiled with self-tests");

   } else {

      if (system_mmap_get_ramdisk(0, NULL, NULL) < 0)
         panic("No ramdisk and no selftest requested: nothing to do.");

      /* Run init or whatever program was passed in the cmdline */
      long rc = first_execve(cmd_args[0], cmd_args);

      if (rc != 0)
         panic("execve('%s') failed with %i\n", cmd_args[0], rc);
   }
}

static void do_async_init()
{
   /* declare the show_hello_message() function */
   void show_hello_message(void);

   mount_initrd();
   init_devfs();
   init_modules();
   init_extra_debug_features();

   show_hello_message();
   run_init_or_selftest();
}

static void
async_init(void)
{
   if (kthread_create(&do_async_init, KTH_ALLOC_BUFS, NULL) < 0)
      panic("Unable to create a kthread for do_async_init()");
}

void
kmain(u32 multiboot_magic, u32 mbi_addr)
{
   call_kernel_global_ctors();
   save_multiboot_info(multiboot_magic, mbi_addr);

   early_init_serial_ports();
   init_cpu_exception_handling();
   early_init_paging();
   early_init_kmalloc();

   read_multiboot_info();
   enable_cpu_features();
   kmain_early_checks();
   init_segmentation();
   init_fpu_memcpy();
   init_kmalloc();
   init_paging();

   acpi_mod_init_tables();

   init_console();
   init_self_tests();
   init_irq_handling();
   init_sched();
   init_syscall_interfaces();
   init_worker_threads();
   init_timer();
   init_system_time();
   init_kernelfs();

   async_init();
   do_schedule();
}
