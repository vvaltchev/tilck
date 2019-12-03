/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/modules.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/kb.h>
#include <tilck/kernel/system_mmap.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/mods/tty.h>
#include <tilck/mods/fb_console.h>
#include <tilck/kernel/cmdline.h>

#include "termutil.h"
#include "dp_int.h"

void dp_write_raw(const char *fmt, ...);
void dp_draw_rect_raw(int row, int col, int h, int w);

static inline void dp_write_header(int i, const char *s, bool selected)
{
   if (selected) {

      dp_write_raw(
         ESC_COLOR_BRIGHT_WHITE "%d" REVERSE_VIDEO "[%s]" RESET_ATTRS " ",
         i, s
      );

   } else {
      dp_write_raw("%d[%s]" RESET_ATTRS " ", i, s);
   }
}

int dp_rows;
int dp_cols;
int dp_start_row;
int dp_end_row;
int dp_start_col;
int dp_screen_start_row;
int dp_screen_rows;
bool ui_need_update;
struct dp_screen *dp_ctx;

static bool in_debug_panel;
static struct tty *dp_tty;
static struct tty *saved_tty;
static struct list dp_screens_list = make_list(dp_screens_list);

static void dp_enter(void)
{
   struct dp_screen *pos;
   struct term *t = get_curr_term();
   term_set_cursor_enabled(t, false);

   in_debug_panel = true;
   dp_rows = term_get_rows(t);
   dp_cols = term_get_cols(t);
   dp_start_row = (dp_rows - DP_H) / 2 + 1;
   dp_start_col = (dp_cols - DP_W) / 2 + 1;
   dp_end_row = dp_start_row + DP_H;
   dp_screen_start_row = dp_start_row + 3;
   dp_screen_rows = (DP_H - 2 - (dp_screen_start_row - dp_start_row));

   list_for_each_ro(pos, &dp_screens_list, node) {

      pos->row_off = 0;
      pos->row_max = 0;

      if (pos->on_dp_enter)
         pos->on_dp_enter();
   }
}

static void dp_exit(void)
{
   struct dp_screen *pos;
   in_debug_panel = false;

   list_for_each_ro(pos, &dp_screens_list, node) {

      if (pos->on_dp_exit)
         pos->on_dp_exit();
   }

   term_set_cursor_enabled(get_curr_term(), true);
}

void dp_register_screen(struct dp_screen *screen)
{
   struct dp_screen *pos;
   struct list_node *pred = (struct list_node *)&dp_screens_list;

   list_for_each_ro(pos, &dp_screens_list, node) {

      if (pos->index < screen->index)
         pred = &pos->node;

      if (pos->index == screen->index)
         panic("[debugpanel] Index conflict while registering %s at %d",
               screen->label, screen->index);
   }

   list_add_after(pred, &screen->node);
}

static int dp_debug_panel_off_keypress(struct key_event ke)
{
   if (kb_is_ctrl_pressed() && ke.key == KEY_F12) {

      if (!dp_tty) {

         dp_tty = create_tty_nodev();

         if (!dp_tty) {
            printk("ERROR: no enough memory for debug panel's TTY\n");
            return KB_HANDLER_OK_AND_STOP;
         }

         dp_ctx = list_first_obj(&dp_screens_list, struct dp_screen, node);
      }

      saved_tty = get_curr_tty();

      if (set_curr_tty(dp_tty) == 0) {
         dp_enter();
         return KB_HANDLER_OK_AND_STOP;
      }
   }

   return KB_HANDLER_NAK;
}

static void redraw_screen(void)
{
   struct dp_screen *pos;
   char buf[64];
   int rc;

   dp_clear();
   dp_move_cursor(dp_start_row + 1, dp_start_col + 2);

   list_for_each_ro(pos, &dp_screens_list, node) {
      dp_write_header(pos->index+1, pos->label, pos == dp_ctx);
   }

   dp_write_header(12, "Quit", false);

   dp_ctx->draw_func();

   dp_draw_rect_raw(dp_start_row, dp_start_col, DP_H, DP_W);
   dp_move_cursor(dp_start_row, dp_start_col + 2);
   dp_write_raw(ESC_COLOR_YELLOW "[ TilckDebugPanel ]" RESET_ATTRS);

   rc = snprintk(buf, sizeof(buf),
                 "[rows %02d - %02d of %02d]",
                 dp_ctx->row_off + 1,
                 MIN(dp_ctx->row_off + dp_screen_rows, dp_ctx->row_max) + 1,
                 dp_ctx->row_max + 1);

   dp_move_cursor(dp_end_row - 1, dp_start_col + DP_W - rc - 2);
   dp_write_raw(ESC_COLOR_BRIGHT_RED "%s" RESET_ATTRS, buf);
   ui_need_update = false;
}

static int dp_keypress_handler(struct key_event ke)
{
   int rc;

   if (kopt_serial_console)
      return KB_HANDLER_NAK;

   if (!ke.pressed)
      return KB_HANDLER_NAK; /* ignore key-release events */

   if (!in_debug_panel) {

      if (dp_debug_panel_off_keypress(ke) == KB_HANDLER_NAK)
         return KB_HANDLER_NAK;

      ui_need_update = true;
   }

   ASSERT(in_debug_panel);

   if (ke.print_char == 'q' || (!kb_is_ctrl_pressed() && ke.key == KEY_F12)) {

      if (set_curr_tty(saved_tty) == 0)
         dp_exit();

      return KB_HANDLER_OK_AND_STOP;
   }

   rc = kb_get_fn_key_pressed(ke.key);

   if (rc > 0) {

      struct dp_screen *pos;
      list_for_each_ro(pos, &dp_screens_list, node) {

         if (pos->index == rc - 1 && pos != dp_ctx) {
            dp_ctx = pos;
            ui_need_update = true;
            break;
         }
      }

   } else if (ke.key == KEY_DOWN) {

      if (dp_ctx->row_off + dp_screen_rows < dp_ctx->row_max) {
         dp_ctx->row_off++;
         ui_need_update = true;
      }

   } else if (ke.key == KEY_UP) {

      if (dp_ctx->row_off > 0) {
         dp_ctx->row_off--;
         ui_need_update = true;
      }
   }

   if (!ui_need_update && dp_ctx->on_keypress_func) {

      rc = dp_ctx->on_keypress_func(ke);

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

static struct keypress_handler_elem debugpanel_handler_elem =
{
   .handler = &dp_keypress_handler
};

static void dp_init(void)
{
   kb_register_keypress_handler(&debugpanel_handler_elem);
}

static struct module dp_module = {

   .name = "debugpanel",
   .init = &dp_init,
};

REGISTER_MODULE(&dp_module);
