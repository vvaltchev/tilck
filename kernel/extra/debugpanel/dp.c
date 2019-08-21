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

void dp_write_raw(const char *fmt, ...);

static inline void dp_write_header(int i, const char *s, bool selected)
{
   if (selected) {
      dp_write_raw("%d" REVERSE_VIDEO "[%s]" RESET_ATTRS " ", i, s);
   } else {
      dp_write_raw("%d[%s]" RESET_ATTRS " ", i, s);
   }
}

int dp_rows;
int dp_cols;
int dp_start_row;
int dp_start_col;
bool ui_need_update;

static bool in_debug_panel;
static tty *dp_tty;
static tty *saved_tty;
static const dp_screen *dp_ctx;
static list dp_screens_list = make_list(dp_screens_list);

static void dp_enter(void)
{
   in_debug_panel = true;
   dp_rows = term_get_rows(get_curr_term());
   dp_cols = term_get_cols(get_curr_term());
   dp_start_row = (dp_rows - DP_H) / 2 + 1;
   dp_start_col = (dp_cols - DP_W) / 2 + 1;
}

static void dp_exit(void)
{
   in_debug_panel = false;
}

void dp_register_screen(dp_screen *screen)
{
   dp_screen *pos;
   list_node *pred = (list_node *)&dp_screens_list;

   list_for_each_ro(pos, &dp_screens_list, node) {

      if (pos->index < screen->index)
         pred = &pos->node;

      if (pos->index == screen->index)
         panic("[debugpanel] Index conflict while registering %s at %d",
               screen->label, screen->index);
   }

   list_add_after(pred, &screen->node);
}

static int dp_debug_panel_off_keypress(u32 key, u8 c)
{
   if (kb_is_ctrl_pressed() && key == KEY_F12) {

      if (!dp_tty) {

         dp_tty = create_tty_nodev();

         if (!dp_tty) {
            printk("ERROR: no enough memory for debug panel's TTY\n");
            return KB_HANDLER_OK_AND_STOP;
         }

         dp_ctx = list_first_obj(&dp_screens_list, dp_screen, node);
      }

      saved_tty = get_curr_tty();

      if (set_curr_tty(dp_tty) == 0)
         dp_enter();

      return KB_HANDLER_OK_AND_STOP;
   }

   return KB_HANDLER_NAK;
}

static void redraw_screen(void)
{
   dp_screen *pos;

   dp_clear();
   dp_move_cursor(dp_start_row + 1, dp_start_col + 2);

   list_for_each_ro(pos, &dp_screens_list, node) {
      dp_write_header(pos->index+1, pos->label, pos == dp_ctx);
   }

   dp_write_header(12, "Quit", false);
   dp_move_cursor(dp_start_row + 3, 1);

   dp_ctx->draw_func();

   dp_draw_rect(dp_start_row, dp_start_col, DP_H, DP_W);
   dp_move_cursor(dp_start_row, dp_start_col + 2);
   dp_write_raw(ESC_COLOR_YELLOW "[ TilckDebugPanel ]" RESET_ATTRS);
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

   if (!kb_is_ctrl_pressed() && (key == KEY_F12 || c == 'q')) {

      if (set_curr_tty(saved_tty) == 0)
         dp_exit();

      return KB_HANDLER_OK_AND_STOP;
   }

   rc = kb_get_fn_key_pressed(key);

   if (rc > 0) {

      dp_screen *pos;
      list_for_each_ro(pos, &dp_screens_list, node) {

         if (pos->index == rc - 1 && pos != dp_ctx) {
            dp_ctx = pos;
            ui_need_update = true;
            break;
         }
      }
   }

   if (!ui_need_update && dp_ctx->on_keypress_func) {

      rc = dp_ctx->on_keypress_func(key, c);

      if (rc == KB_HANDLER_OK_AND_STOP)
         return rc; /* skip redraw_screen() */
   }


   if (ui_need_update) {
      term_pause_video_output(get_curr_term());
      redraw_screen();
      term_restart_video_output(get_curr_term());
   }

   return KB_HANDLER_OK_AND_STOP;
}

static keypress_handler_elem debugpanel_handler_elem =
{
   .handler = &dp_keypress_handler
};

__attribute__((constructor))
static void init_debug_panel()
{
   kb_register_keypress_handler(&debugpanel_handler_elem);
}
