/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/kb.h>
#include <tilck/kernel/system_mmap.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/tty.h>
#include <tilck/kernel/fb_console.h>
#include <tilck/kernel/cmdline.h>

#include "termutil.h"
#include "dp_int.h"

int dp_rows;
int dp_cols;
static bool in_debug_panel;
static bool ui_need_update;
static tty *dp_tty;
static tty *saved_tty;
static const dp_context *dp_ctx;

static void func2_memmap(void)
{
   dp_move_cursor(dp_rows / 2, 0);
   printk(NO_PREFIX "func2: memmap");
}

static void func3_heaps(void)
{
   dp_move_cursor(dp_rows / 2, 0);
   printk(NO_PREFIX "func3: heaps");
}

static const dp_context dp_contexts[] =
{
   {
      .label = "Options",
      .draw_func = dp_show_opts,
      .on_keypress_func = NULL,
   },

   {
      .label = "MemMap",
      .draw_func = func2_memmap,
      .on_keypress_func = NULL,
   },

   {
      .label = "Heaps",
      .draw_func = func3_heaps,
      .on_keypress_func = NULL,
   },

   {
      .label = "Tasks",
      .draw_func = do_show_tasks,
      .on_keypress_func = NULL,
   },

   {
      .label = "IRQs",
      .draw_func = dp_show_irq_stats,
      .on_keypress_func = NULL,
   },
};

static int dp_debug_panel_off_keypress(u32 key, u8 c)
{
   if (kb_is_ctrl_pressed() && key == KEY_F12) {

      if (!dp_tty) {

         dp_tty = create_tty_nodev();

         if (!dp_tty) {
            printk("ERROR: no enough memory for debug panel's TTY\n");
            return KB_HANDLER_OK_AND_STOP;
         }

         dp_ctx = &dp_contexts[0];
      }

      saved_tty = get_curr_tty();

      if (set_curr_tty(dp_tty) == 0) {
         in_debug_panel = true;
         dp_rows = term_get_rows(get_curr_term());
         dp_cols = term_get_cols(get_curr_term());
      }

      return KB_HANDLER_OK_AND_STOP;
   }

   return KB_HANDLER_NAK;
}

static void redraw_screen(void)
{
   dp_clear();
   dp_move_cursor(0, 0);
   dp_printk(COLOR_YELLOW "[TilckDebugPanel] " RESET_ATTRS);

   for (int i = 0; i < (int)ARRAY_SIZE(dp_contexts); i++) {
      dp_write_header(i+1, dp_contexts[i].label, dp_ctx == &dp_contexts[i]);
   }

   dp_write_header(12, "Quit", false);
   dp_ctx->draw_func();

   dp_move_cursor(999,999);
   ui_need_update = false;
}

static int dp_keypress_handler(u32 key, u8 c)
{
   int rc;

   if (kopt_serial_console)
      return KB_HANDLER_NAK;

   if (!in_debug_panel) {

      if (dp_debug_panel_off_keypress(key, c) == KB_HANDLER_NAK)
         return KB_HANDLER_NAK;

      ui_need_update = true;
   }

   if (!kb_is_ctrl_pressed() && key == KEY_F12) {

      if (set_curr_tty(saved_tty) == 0) {
         in_debug_panel = false;
      }

      return KB_HANDLER_OK_AND_STOP;
   }

   rc = kb_get_fn_key_pressed(key);

   if (rc > 0 && rc <= (int)ARRAY_SIZE(dp_contexts)) {

      /* key F1 .. F5 pressed */

      if (dp_ctx != &dp_contexts[rc-1]) {
         dp_ctx = &dp_contexts[rc-1];
         ui_need_update = true;
      }

   } else {

      if (dp_ctx->on_keypress_func) {
         rc = dp_ctx->on_keypress_func(key, c);

         if (rc == KB_HANDLER_OK_AND_STOP)
            return rc; /* skip redraw_screen() */
      }
   }

   if (ui_need_update)
      redraw_screen();

   // switch (key) {
   //    case KEY_F2:
   //       debug_kmalloc_dump_mem_usage();
   //       return KB_HANDLER_OK_AND_STOP;
   //    case KEY_F3:
   //       dump_system_memory_map();
   //       return KB_HANDLER_OK_AND_STOP;
   // }

   return KB_HANDLER_OK_AND_STOP;
}

static keypress_handler_elem debug_keypress_handler_elem =
{
   .handler = &dp_keypress_handler
};

__attribute__((constructor))
static void init_debug_panel()
{
   kb_register_keypress_handler(&debug_keypress_handler_elem);
}
