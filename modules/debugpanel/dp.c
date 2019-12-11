/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/syscalls.h>

#include <tilck/kernel/modules.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/kb.h>
#include <tilck/kernel/system_mmap.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/tty.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/cmdline.h>
#include <tilck/kernel/fs/vfs.h>

#include <tilck/mods/fb_console.h>

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

static bool dp_enable_kb_handler;
static bool in_debug_panel;
static struct tty *dp_tty;
static struct tty *saved_tty;
static struct list dp_screens_list = make_list(dp_screens_list);

static void dp_enter(void)
{
   struct term_params tparams;
   struct dp_screen *pos;

   term_read_info(&tparams);
   dp_set_cursor_enabled(false);

   in_debug_panel = true;
   dp_rows = tparams.rows;
   dp_cols = tparams.cols;
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

   dp_set_cursor_enabled(true);
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

static int
dp_switch_to_dedicated_video_tty(void)
{
   int rc = 0;

   if (!dp_tty) {

      dp_tty = create_tty_nodev();

      if (!dp_tty) {
         printk("ERROR: no enough memory for debug panel's TTY\n");
         rc = -ENOMEM;
         goto end;
      }

      dp_ctx = list_first_obj(&dp_screens_list, struct dp_screen, node);
   }

   saved_tty = get_curr_tty();

   if ((rc = set_curr_tty(dp_tty)) < 0)
      goto end;

   dp_enter();

end:
   return rc;
}

static enum kb_handler_action
dp_debug_panel_off_keypress(struct kb_dev *kb, struct key_event ke)
{
   int rc;

   if (kb_is_ctrl_pressed(kb) && ke.key == KEY_F12) {

      rc = dp_switch_to_dedicated_video_tty();

      if (!rc || rc == -ENOMEM)
         return kb_handler_ok_and_stop;
   }

   return kb_handler_nak;
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

   if (dp_enable_kb_handler)
      dp_write_raw(ESC_COLOR_YELLOW "[ TilckDebugPanel ]" RESET_ATTRS);
   else
      dp_write_raw(ESC_COLOR_YELLOW "[ TilckDebugPanel (cmd) ]" RESET_ATTRS);

   rc = snprintk(buf, sizeof(buf),
                 "[rows %02d - %02d of %02d]",
                 dp_ctx->row_off + 1,
                 MIN(dp_ctx->row_off + dp_screen_rows, dp_ctx->row_max) + 1,
                 dp_ctx->row_max + 1);

   dp_move_cursor(dp_end_row - 1, dp_start_col + DP_W - rc - 2);
   dp_write_raw(ESC_COLOR_BRIGHT_RED "%s" RESET_ATTRS, buf);
   ui_need_update = false;
}

static int
dp_main_body(enum term_type tt, struct key_event ke)
{
   int rc;
   bool dp_screen_key_handled = false;

   if ('0' <= ke.print_char && ke.print_char <= '9') {

      struct dp_screen *pos;
      rc = ke.print_char - '0';

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

      if (rc == kb_handler_ok_and_stop)
         return 1; /* skip redraw_screen() */

      if (rc != kb_handler_nak)
         dp_screen_key_handled = true;
   }

   if (ui_need_update) {

      if (tt == term_type_video)
         term_pause_video_output();

      redraw_screen();

      if (tt == term_type_video)
         term_restart_video_output();
   }

   return dp_screen_key_handled;
}

static void
dp_video_quit(void)
{
   if (set_curr_tty(saved_tty) == 0)
      dp_exit();
}

static enum kb_handler_action
dp_keypress_handler(struct kb_dev *kb, struct key_event ke)
{
   int rc;
   u8 mods;

   if (!ke.pressed)
      return kb_handler_nak; /* ignore key-release events */

   if (kopt_serial_console)
      return kb_handler_nak;

   if (!dp_enable_kb_handler) {

      /* Disable switch to any other TTY with ALT + Fn */
      if (kb_is_alt_pressed(kb)) {
         if (kb_get_fn_key_pressed(ke.key) > 0)
            return kb_handler_ok_and_stop;
      }

      return kb_handler_nak;
   }

   if (!in_debug_panel) {

      if (dp_debug_panel_off_keypress(kb, ke) == kb_handler_nak)
         return kb_handler_nak;

      ui_need_update = true;
   }

   ASSERT(in_debug_panel);
   mods = kb_get_current_modifiers(kb);

   if (mods == 4 /* ctrl */ && ke.print_char == 'c') {
      dp_video_quit();
      return kb_handler_ok_and_stop;
   }

   rc = dp_main_body(term_type_video, ke);

   if (!rc && ke.print_char == 'q') {
      dp_video_quit();
      return kb_handler_ok_and_stop;
   }

   return kb_handler_ok_and_stop;
}

static struct keypress_handler_elem debugpanel_handler_elem =
{
   .handler = &dp_keypress_handler
};


static int
read_ke_from_tty(fs_handle h, struct key_event *ke)
{
   char buf[16];
   int rc;
   u32 len;

   enum {

      state_default,
      state_in_esc1,
      state_in_csi_param,
      state_in_csi_intermediate,

   } state = state_default;

   bzero(ke, sizeof(*ke));
   bzero(buf, sizeof(buf));

   for (len = 0; len < sizeof(buf); len++) {

      if ((rc = vfs_read(h, &buf[len], 1)) <= 0)
         return -1;

   state_changed:

      switch (state) {

         case state_in_csi_intermediate:

            if (IN_RANGE_INC(buf[len], 0x20, 0x2F))
               continue; /* for loop */

            /*
             * The current char must be in range 0x40-0x7E, but we must break
             * anyway, even it isn't.
             */

            break; /* switch (state) */

         case state_in_csi_param:

            if (IN_RANGE_INC(buf[len], 0x30, 0x3F))
               continue; /* for loop */

            state = state_in_csi_intermediate;
            goto state_changed;

         case state_in_esc1:

            if (buf[len] == '[') {
               state = state_in_csi_param;
               continue; /* for loop */
            }

            /* any other non-CSI sequence is ignored */
            break; /* switch (state) */

         case state_default:

            if (buf[len] == '\033') {
               state = state_in_esc1;
               continue; /* for loop */
            }

            break; /* switch (state) */

         default:
            NOT_REACHED();
      }

      break; /* for (len = 0; len < sizeof(buf); len++) */
   }

   if (IN_RANGE_INC(buf[0], 32, 127) || IN_RANGE_INC(buf[0], 1, 26)) {

      *ke = (struct key_event) {
         .pressed = true,
         .print_char = buf[0],
         .key = 0,
      };

      return 0;

   } else if (buf[0] == '\033' && buf[1] == '[') {

      u32 key = 0;

      switch (buf[2]) {

         case 'A':
            key = KEY_UP;
            break;

         case 'B':
            key = KEY_DOWN;
            break;

         case 'C':
            key = KEY_RIGHT;
            break;

         case 'D':
            key = KEY_LEFT;
            break;
      }

      *ke = (struct key_event) {
         .pressed = true,
         .print_char = 0,
         .key = key,
      };
   }

   return 0;
}

static void dp_tilck_cmd()
{
   enum term_type tt;
   struct key_event ke;
   fs_handle h = NULL;
   int rc;

   dp_enable_kb_handler = false;
   tt = get_curr_proc_tty_term_type();

   dp_ctx = list_first_obj(&dp_screens_list, struct dp_screen, node);
   dp_enter();

   if (tt == term_type_video) {

      if ((rc = vfs_open("/dev/tty0", &h, O_RDONLY, 0777)) < 0)
         goto end;

   } else {

      /*
       * HACK: this assumes that, if we're using a serial tty, it's TTYS0.
       * TOOD: fix this somehow. An idea could be exposing a special interface
       * from tty to open a handle to it, without any access to the VFS layer.
       */
      if ((rc = vfs_open("/dev/ttyS0", &h, O_RDONLY, 0777)) < 0)
         goto end;
   }

   tty_set_raw_mode(get_curr_process_tty());

   bzero(&ke, sizeof(ke));
   ui_need_update = true;
   dp_main_body(tt, ke);

   while (true) {

      if (read_ke_from_tty(h, &ke) < 0)
         break;

      if (ke.print_char == 0x3 /* Ctrl+C */)
         break;

      rc = dp_main_body(tt, ke);

      if (!rc && ke.print_char == 'q')
         break;
   }

end:

   if (h)
      vfs_close(h);

   tty_reset_termios(get_curr_process_tty());
   dp_exit();
   dp_clear();
   dp_write_raw("\n");
   dp_enable_kb_handler = true;
}

static void dp_init(void)
{
   dp_enable_kb_handler = true;
   register_keypress_handler(&debugpanel_handler_elem);
   register_tilck_cmd(TILCK_CMD_DEBUG_PANEL, dp_tilck_cmd);
}

static struct module dp_module = {

   .name = "debugpanel",
   .priority = 100,
   .init = &dp_init,
};

REGISTER_MODULE(&dp_module);
