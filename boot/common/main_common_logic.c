/* SPDX-License-Identifier: BSD-2-Clause */

#define USE_ELF32

#include <tilck_gen_headers/config_boot.h>
#include <tilck_gen_headers/config_kernel.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/failsafe_assert.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>
#include <tilck/common/color_defs.h>
#include <tilck/common/elf_calc_mem_size.c.h>
#include <tilck/common/elf_get_section.c.h>
#include <tilck/common/build_info.h>

#include "common_int.h"

#define CHECK(cond)                                  \
   do {                                              \
      if (!(cond)) {                                 \
         printk("CHECK '%s' FAILED\n", #cond);       \
         return false;                               \
      }                                              \
   } while(0)

static video_mode_t selected_mode = INVALID_VIDEO_MODE;
static char kernel_file_path[64] = KERNEL_FILE_PATH;
static char line_buf[64];
static void *kernel_elf_file_paddr;
static struct build_info *kernel_build_info;

void
write_bootloader_hello_msg(void)
{
   intf->set_color(COLOR_BRIGHT_WHITE);

   printk("----- Hello from Tilck's %s bootloader! -----\n\n",
          intf->efi ? "UEFI" : "legacy");

   intf->set_color(DEFAULT_FG_COLOR);
}

void *
load_kernel_image(void)
{
   return simple_elf_loader(kernel_elf_file_paddr);
}

void
write_ok_msg(void)
{
   intf->set_color(COLOR_GREEN);
   printk("[  OK  ]\n");
   intf->set_color(DEFAULT_FG_COLOR);
}

void
write_fail_msg(void)
{
   intf->set_color(COLOR_RED);
   printk("[ FAIL ]\n");
   intf->set_color(DEFAULT_FG_COLOR);
}

static bool
check_elf_kernel(void)
{
   Elf_Ehdr *header = kernel_elf_file_paddr;
   Elf_Phdr *phdr = (Elf_Phdr *)(header + 1);

   if (header->e_ident[EI_MAG0] != ELFMAG0 ||
       header->e_ident[EI_MAG1] != ELFMAG1 ||
       header->e_ident[EI_MAG2] != ELFMAG2 ||
       header->e_ident[EI_MAG3] != ELFMAG3)
   {
      printk("Not an ELF file\n");
      return false;
   }

   if (header->e_ehsize != sizeof(*header)) {
      printk("Not an ELF32 file\n");
      return false;
   }

   for (int i = 0; i < header->e_phnum; i++, phdr++) {

      // Ignore non-load segments.
      if (phdr->p_type != PT_LOAD)
         continue;

      CHECK(phdr->p_vaddr >= KERNEL_BASE_VA);
      CHECK(phdr->p_paddr >= KERNEL_PADDR);
   }

   return true;
}

size_t
get_loaded_kernel_mem_sz(void)
{
   if (!kernel_elf_file_paddr)
      panic("No loaded kernel");

   return elf_calc_mem_size(kernel_elf_file_paddr);
}

static bool
load_kernel_file(void)
{
   Elf_Shdr *section;
   Elf_Ehdr *header;

   printk("Loading the ELF kernel... ");

   if (!intf->load_kernel_file(kernel_file_path, &kernel_elf_file_paddr)) {
      write_fail_msg();
      return false;
   }

   if (!check_elf_kernel()) {
      write_fail_msg();
      return false;
   }

   header = kernel_elf_file_paddr;
   section = elf_get_section(header, ".tilck_info");

   if (!section) {
      printk("Not a Tilck ELF kernel file\n");
      write_fail_msg();
      return false;
   }

   kernel_build_info = (void *)((char *)header + section->sh_offset);
   write_ok_msg();
   return true;
}

static void
read_kernel_file_path(void)
{
   bool failed = false;

   while (true) {

      printk("Kernel file path: ");
      read_line(line_buf, sizeof(line_buf));

      if (!line_buf[0] && !failed) {
         printk("Keeping the current kernel file.\n");
         break;
      }

      if (line_buf[0] != '/') {
         printk("Invalid file path. Expected an absolute path.\n");
         continue;
      }

      strcpy(kernel_file_path, line_buf);

      if (!load_kernel_file()) {
         failed = true;
         continue;
      }

      break;
   }
}

static void
clear_screen(void)
{
   intf->clear_screen();
   write_bootloader_hello_msg();
}

static bool
run_interactive_logic(void)
{
   struct generic_video_mode_info gi;
   bool wait_for_key, dirty_comm;
   char *commit_str;
   char buf[8];

   while (true) {

      wait_for_key = true;

      if (!intf->get_mode_info(selected_mode, &gi)) {
         printk("ERROR: get_mode_info() failed");
         return false;
      }

      dirty_comm = !strncmp(kernel_build_info->commit, "dirty:", 6);
      commit_str = kernel_build_info->commit + (dirty_comm ? 6 : 0);

      printk("Menu:\n");
      printk("---------------------------------------------------\n");
      printk("k) Kernel file: %s\n", kernel_file_path);
      printk("     version: %s\n", kernel_build_info->ver);
      printk("     commit:  %s%s\n", commit_str, dirty_comm ? " (dirty)" : "");
      printk("     modules: %s\n", kernel_build_info->modules_list);
      printk("\n");
      printk("v) Video mode:  ");
      show_mode(-1, &gi, false);
      printk("b) Boot\n");

      printk("\n> ");
      read_line(buf, sizeof(buf));

      switch (buf[0]) {

         case 0:
         case 'b':
            return true;

         case 'k':
            read_kernel_file_path();
            break;

         case 'v':
            show_video_modes();
            printk("\n");
            selected_mode = get_user_video_mode_choice();
            wait_for_key = false;
            break;

         default:
            printk("Invalid command\n");
      }

      if (wait_for_key) {
         printk("Press ANY key to continue");
         intf->read_key();
      }

      clear_screen();
   }

   return true;
}

static void
wait_for_any_key(void)
{
   printk("Press ANY key to continue");
   intf->read_key();
}

bool
common_bootloader_logic(void)
{
   bool in_retry = false;

   fetch_all_video_modes_once();
   selected_mode = g_defmode;

   if (!load_kernel_file())
      return false;

   printk("\n");

retry:

   if (in_retry) {
      wait_for_any_key();
      clear_screen();
   }

   in_retry = true;

   if (BOOT_INTERACTIVE) {
      if (!run_interactive_logic())
         return false;
   }

   clear_screen();

   if (!intf->load_initrd()) {

      if (BOOT_INTERACTIVE)
         goto retry;

      return false;
   }

   if (!intf->set_curr_video_mode(selected_mode)) {

      if (BOOT_INTERACTIVE) {
         printk("ERROR: cannot set the selected video mode\n");
         goto retry;
      }

      /* In this other case, the current video mode will be kept */
   }

   return true;
}
