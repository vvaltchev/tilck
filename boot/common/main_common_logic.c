/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_boot.h>
#include <tilck_gen_headers/config_kernel.h>
#include <tilck_gen_headers/mod_console.h>
#include <tilck_gen_headers/mod_serial.h>
#include <tilck_gen_headers/mod_fb.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/failsafe_assert.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>

#include "common_int.h"

static video_mode_t selected_mode = INVALID_VIDEO_MODE;

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
   fetch_all_video_modes_once();
   selected_mode = g_defmode;

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
