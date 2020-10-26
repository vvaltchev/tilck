/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_boot.h>
#include <tilck_gen_headers/config_kernel.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/failsafe_assert.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>
#include <tilck/common/color_defs.h>

#include "common_int.h"

static video_mode_t selected_mode = INVALID_VIDEO_MODE;

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
run_interactive_logic(void)
{
   show_video_modes();
   selected_mode = get_user_video_mode_choice();
   return true;
}

bool
common_bootloader_logic(void)
{
   void *kernel_file;

   fetch_all_video_modes_once();
   selected_mode = g_defmode;

   printk("Loading the ELF kernel... ");

   if (!intf->load_kernel_file(KERNEL_FILE_PATH, &kernel_file)) {
      write_fail_msg();
      return false;
   }

   write_ok_msg();
   printk("\n");

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
