
#include <common/basic_defs.h>
#include <common/string_util.h>
#include <common/utils.h>

#include <multiboot.h>

#include <exos/hal.h>
#include <exos/irq.h>
#include <exos/kmalloc.h>
#include <exos/paging.h>
#include <exos/debug_utils.h>
#include <exos/process.h>
#include <exos/elf_loader.h>
#include <exos/tasklet.h>
#include <exos/sync.h>
#include <exos/fs/fat32.h>
#include <exos/fs/exvfs.h>
#include <exos/fs/devfs.h>
#include <exos/kb.h>
#include <exos/timer.h>
#include <exos/term.h>
#include <exos/pageframe_allocator.h>
#include <exos/arch/generic_x86/textmode_video.h>

extern u32 memsize_in_mb;
extern uptr ramdisk_paddr;
extern size_t ramdisk_size;

/* Variables used by the cmdline parsing code */

static bool no_init;
static void (*self_test_to_run)(void);

/* -- */

void show_hello_message(void)
{
   printk("Hello from exOS! [%s build, GCC %i.%i.%i]\n",
          BUILDTYPE_STR, __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
}

void use_kernel_arg(int arg_num, const char *arg)
{
   //printk("Kernel arg[%i]: '%s'\n", arg_num, arg);

   const size_t arg_len = strlen(arg);

   if (!strcmp(arg, "-noinit")) {
      no_init = true;
      return;
   }

   if (arg_len >= 3) {
      if (arg[0] == '-' && arg[1] == 's' && arg[2] == '=') {
         const char *a2 = arg + 3;
         char buf[256] = "selftest_";

         printk("*** Run selftest: '%s' ***\n", a2);

         memcpy(buf+strlen(buf), a2, strlen(a2) + 1);
         uptr addr = find_addr_of_symbol(buf);

         if (!addr) {
            panic("Self test function '%s' not found.\n", buf);
         }

         self_test_to_run = (void (*)(void)) addr;
         return;
      }
   }
}

void parse_kernel_cmdline(const char *cmdline)
{
   char buf[256];
   char *dptr = buf;
   const char *ptr = cmdline;
   int args_count = 0;

   while (*ptr) {

      if (*ptr == ' ' || (dptr-buf >= (sptr)sizeof(buf)-1)) {
         *dptr = 0;
         dptr = buf;
         ptr++;
         use_kernel_arg(args_count++, buf);
         continue;
      }

      *dptr++ = *ptr++;
   }

   if (dptr != buf) {
      *dptr = 0;
      use_kernel_arg(args_count++, buf);
   }
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

   if (mbi->flags & MULTIBOOT_INFO_CMDLINE) {
      parse_kernel_cmdline((const char *)(uptr)mbi->cmdline);
   }
}

void show_additional_info(void)
{
   printk("TIMER_HZ: %i; MEM: %i MB\n",
           TIMER_HZ, get_phys_mem_mb());
}

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


void init_tty(void);

/*
 * For the moment, exOS supports only the standard PC text mode (80x25),
 * but, thanks to the video_interface, term can work with anything, even
 * with a graphical framebuffer.
 */
static const video_interface x86_pc_text_mode_vi =
{
   textmode_set_char_at,
   textmode_clear_row,
   textmode_scroll_up,
   textmode_scroll_down,
   textmode_is_at_bottom,
   textmode_scroll_to_bottom,
   textmode_add_row_and_scroll,
   textmode_move_cursor,
   textmode_enable_cursor,
   textmode_disable_cursor
};

sptr sys_execve(const char *filename,
                const char *const *argv,
                const char *const *env);

void kmain(u32 multiboot_magic, u32 mbi_addr)
{
   init_term(&x86_pc_text_mode_vi, make_color(COLOR_WHITE, COLOR_BLACK));
   show_hello_message();
   read_multiboot_info(multiboot_magic, mbi_addr);
   show_additional_info();

   setup_segmentation();
   setup_soft_interrupt_handling();
   setup_irq_handling();

   init_pageframe_allocator(); /* NOTE: unused at the moment */

   init_paging();
   init_kmalloc();
   init_paging_cow();

   init_sched();
   init_tasklets();

   timer_set_freq(TIMER_HZ);

   irq_install_handler(X86_PC_TIMER_IRQ, timer_handler);
   irq_install_handler(X86_PC_KEYBOARD_IRQ, keyboard_handler);

   DEBUG_CHECKED_SUCCESS(enqueue_tasklet0(&init_kb));

   setup_sysenter_interface(); /* TODO: complete the sysenter support */

   mount_ramdisk();

   create_and_register_devfs();
   init_tty();

   if (self_test_to_run)
      self_test_to_run();

   if (ramdisk_size && !no_init) {
     sptr rc = sys_execve("/sbin/init", NULL, NULL);
     panic("execve(\"/sbin/init\") failed with %i\n", rc);
   }

   switch_to_idle_task_outside_interrupt_context();
}
