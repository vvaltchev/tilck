/* SPDX-License-Identifier: BSD-2-Clause */

#define USE_ELF32

#include <tilck_gen_headers/config_boot.h>
#include <tilck_gen_headers/config_kernel.h>
#include <tilck_gen_headers/krn_max_sz.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/failsafe_assert.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>
#include <tilck/common/color_defs.h>
#include <tilck/common/elf_calc_mem_size.c.h>

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
static void *kernel_paddr;

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
   Elf32_Ehdr *header = kernel_paddr;
   Elf32_Phdr *phdr = (Elf32_Phdr *)(header + 1);

   CHECK(header->e_ident[EI_MAG0] == ELFMAG0);
   CHECK(header->e_ident[EI_MAG1] == ELFMAG1);
   CHECK(header->e_ident[EI_MAG2] == ELFMAG2);
   CHECK(header->e_ident[EI_MAG3] == ELFMAG3);
   CHECK(header->e_ehsize == sizeof(*header));

   for (int i = 0; i < header->e_phnum; i++, phdr++) {

      // Ignore non-load segments.
      if (phdr->p_type != PT_LOAD)
         continue;

      CHECK(phdr->p_vaddr >= KERNEL_BASE_VA);
      CHECK(phdr->p_paddr >= KERNEL_PADDR);
   }

   CHECK(elf_calc_mem_size(header) <= KERNEL_MAX_SIZE);
   return true;
}

static bool
load_kernel_file(void)
{
   printk("Loading the ELF kernel... ");

   if (!intf->load_kernel_file(kernel_file_path, &kernel_paddr)) {
      write_fail_msg();
      return false;
   }

   if (!check_elf_kernel()) {
      write_fail_msg();
      return false;
   }

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

static bool
run_interactive_logic(void)
{
   struct generic_video_mode_info gi;
   char buf[8];

   while (true) {

      printk("\n\n");

      if (!intf->get_mode_info(selected_mode, &gi)) {
         printk("ERROR: get_mode_info() failed");
         return false;
      }

      printk("Kernel file (k): %s\n", kernel_file_path);
      printk("Video mode (v):  ");
      show_mode(-1, &gi, false);

      printk("---------------------------------------\n");
      printk("Command (ENTER to boot): ");
      read_line(buf, sizeof(buf));

      switch (buf[0]) {

         case 0:
            return true;

         case 'k':
            read_kernel_file_path();
            break;

         case 'v':
            show_video_modes();
            selected_mode = get_user_video_mode_choice();
            break;

         default:
            printk("Invalid command\n");
      }
   }

   return true;
}

bool
common_bootloader_logic(void)
{
   fetch_all_video_modes_once();
   selected_mode = g_defmode;

   if (!load_kernel_file())
      return false;

retry:

   if (BOOT_INTERACTIVE) {
      if (!run_interactive_logic())
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
