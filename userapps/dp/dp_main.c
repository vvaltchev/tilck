/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Userspace screen registry + main event loop, mirroring the kernel's
 * modules/debugpanel/dp.c (the dp_register_screen / redraw_screen /
 * dp_main_handle_keypress / dp_main_body code). The kernel's
 * dp_common_entry — which set up the TTY in raw mode and drove the
 * read-key/redraw loop — is split across here and dp_screen.c
 * (tui_term_setup / tui_term_restore handle the termios + alt-buffer
 * dance).
 *
 * Screens register themselves at program startup via
 * __attribute__((constructor)) — same pattern as the kernel module
 * (only the call site is the C runtime instead of REGISTER_MODULE).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "term.h"
#include "tui_input.h"
#include "tui_layout.h"
#include "dp_int.h"
#include "dp_panel.h"

static struct dp_screen *dp_screens_head;
static bool skip_next_keypress;

/*
 * Three-phase redraw state. A scroll keypress only flips need_paint;
 * a panel-content change (selection move, data refresh, etc.) flips
 * need_buffer + need_paint; a panel switch (or a modal overlay needing
 * cleanup) flips all three.
 *
 *   need_chrome — clear the screen, redraw the panel-tabs header, the
 *                 outer border, and the title.
 *   need_buffer — call dp_ctx->draw_func() to refill the in-memory line
 *                 buffer with the panel's full content.
 *   need_paint  — emit the visible window of the buffer onto the panel
 *                 content area + redraw the "rows X-Y of Z" footer.
 */
static bool need_chrome;
static bool need_buffer;
static bool need_paint;

void dp_force_full_redraw(void)
{
   need_chrome = true;
   need_buffer = true;
   need_paint  = true;
   ui_need_update = true;
}

void dp_register_screen(struct dp_screen *screen)
{
   /* Insert in ascending `index` order. */
   struct dp_screen **pp = &dp_screens_head;

   while (*pp && (*pp)->index < screen->index)
      pp = &(*pp)->next;

   if (*pp && (*pp)->index == screen->index) {

      fprintf(stderr,
              "dp: index conflict at %d between %s and %s\n",
              screen->index, screen->label, (*pp)->label);
      exit(1);
   }

   screen->next = *pp;
   *pp = screen;
}

static void dp_enter(void)
{
   struct dp_screen *p;

   tui_init_layout();
   tui_term_setup();

   for (p = dp_screens_head; p; p = p->next) {

      p->row_off = 0;
      p->row_max = 0;

      if (p->first_setup)
         p->first_setup();

      if (p->on_dp_enter)
         p->on_dp_enter();
   }
}

static void dp_exit(void)
{
   struct dp_screen *p;

   for (p = dp_screens_head; p; p = p->next) {

      if (p->on_dp_exit)
         p->on_dp_exit();
   }

   tui_term_restore();
}

static void
dp_write_header(int i, const char *s, bool selected)
{
   if (selected) {

      term_write(
         E_COLOR_BR_WHITE "%d" REVERSE_VIDEO "[%s]" RESET_ATTRS " ",
         i, s
      );

   } else {

      term_write("%d[%s]" RESET_ATTRS " ", i, s);
   }
}

static void paint_chrome(void)
{
   struct dp_screen *p;

   term_clear();
   term_move_cursor(tui_start_row + 1, tui_start_col + 2);

   for (p = dp_screens_head; p; p = p->next)
      dp_write_header(p->index + 1, p->label, p == dp_ctx);

   term_write("q[Quit]" RESET_ATTRS " ");

   term_draw_rect_raw(tui_start_row, tui_start_col, DP_H, DP_W);
   term_move_cursor(tui_start_row, tui_start_col + 2);
   term_write(E_COLOR_YELLOW "[ TilckDebugPanel ]" RESET_ATTRS);
}

static void rebuild_buffer(void)
{
   dp_buf_reset();
   dp_ctx->row_max = 0;

   if (dp_ctx->draw_func)
      dp_ctx->draw_func();
}

static void paint_panel(void)
{
   /*
    * Visible content rows: tui_screen_start_row .. tui_end_row-2
    * (inclusive). screen_rows is already the count of those.
    */
   dp_buf_paint(dp_ctx->row_off,
                tui_screen_rows,
                tui_screen_start_row,
                tui_start_col,
                DP_W,
                dp_ctx->static_rows);
}

static void paint_footer(void)
{
   char buf[64];
   int rc;

   /*
    * The "rows X - Y of Z" footer reflects the SCROLLABLE region
    * only; the static area at the top is always visible and not part
    * of the scroll counter. With static_rows=0 the math degenerates
    * to the original buffer-relative numbers.
    */
   const int scroll_rows  = tui_screen_rows - dp_ctx->static_rows;
   const int scroll_total = dp_ctx->row_max - dp_ctx->static_rows + 1;

   int row_to = dp_ctx->row_off + scroll_rows;

   if (row_to > scroll_total)
      row_to = scroll_total;

   rc = snprintf(buf, sizeof(buf),
                 "[rows %02d - %02d of %02d]",
                 dp_ctx->row_off + 1,
                 row_to,
                 scroll_total);

   /* Sit on the bottom border line, near the right edge. */
   term_move_cursor(tui_end_row - 1, tui_start_col + DP_W - rc - 2);
   term_write(E_COLOR_BR_RED "%s" RESET_ATTRS, buf);
}

static void redraw_screen(void)
{
   if (need_chrome) {
      paint_chrome();
      need_chrome = false;
      need_paint = true;       /* chrome paint clears the screen */
   }

   if (need_buffer) {
      rebuild_buffer();
      need_buffer = false;
      need_paint = true;
   }

   if (need_paint) {
      paint_panel();
      paint_footer();
      need_paint = false;
   }

   /* Park the cursor below the panel so it is inconspicuous. */
   term_move_cursor(tui_rows, 1);
   ui_need_update = false;
}

static void
dp_main_handle_keypress(struct key_event ke)
{
   struct dp_screen *p;
   int idx;

   if ('0' <= ke.print_char && ke.print_char <= '9') {

      idx = ke.print_char - '0' - 1;

      for (p = dp_screens_head; p; p = p->next) {

         if (p->index == idx && p != dp_ctx) {
            dp_ctx = p;
            /* Switching panel: redraw chrome (selected tab moves) +
             * rebuild this panel's buffer + paint everything. */
            need_chrome = true;
            need_buffer = true;
            need_paint  = true;
            ui_need_update = true;
            break;
         }
      }

   } else if (!strcmp(ke.seq, TUI_KEY_PAGE_DOWN)) {

      /*
       * Scroll one row down if there is any unviewed content below
       * the viewport. Last visible relrow is row_off + screen_rows -
       * 1, so unviewed content exists when that's strictly less than
       * row_max — equivalently, row_off + screen_rows <= row_max.
       * Strict `<` was off by one and stranded the last row.
       */
      if (dp_ctx->row_off + tui_screen_rows <= dp_ctx->row_max) {
         dp_ctx->row_off++;
         /* Pure scroll: same buffer, just re-clip the viewport. */
         need_paint = true;
         ui_need_update = true;
      }

   } else if (!strcmp(ke.seq, TUI_KEY_PAGE_UP)) {

      if (dp_ctx->row_off > 0) {
         dp_ctx->row_off--;
         need_paint = true;
         ui_need_update = true;
      }
   }
}

static int
dp_main_body(struct key_event ke)
{
   bool dp_screen_key_handled = false;
   int rc;

   if (!skip_next_keypress) {

      dp_main_handle_keypress(ke);

      if (!ui_need_update && dp_ctx && dp_ctx->on_keypress_func) {

         rc = dp_ctx->on_keypress_func(ke);

         /*
          * A panel handler signaling "something changed" (ui_need_update)
          * means the panel content itself is different — selection
          * cursor moved, data was refreshed, sort order changed. Default
          * to a buffer rebuild + repaint; the chrome stays untouched.
          */
         if (ui_need_update) {
            need_buffer = true;
            need_paint  = true;
         }

         if (rc == dp_kb_handler_ok_and_stop)
            return 1; /* skip redraw_screen() */

         if (rc != dp_kb_handler_nak)
            dp_screen_key_handled = true;
      }

   } else {

      /*
       * The previous iteration painted a modal overlay on top of the
       * panel and asked for the next keypress to be eaten. Now we
       * unwind the overlay: chrome may have been overlapped, panel
       * content may have been overwritten — repaint the lot. The
       * buffer is still valid (the overlay didn't go through it).
       */
      skip_next_keypress = false;
      need_chrome = true;
      need_paint  = true;
      ui_need_update = true;
   }

   if (ui_need_update || modal_msg) {

      redraw_screen();

      if (modal_msg) {
         dp_show_modal_msg(modal_msg);
         modal_msg = NULL;
         skip_next_keypress = true;
      }
   }

   return dp_screen_key_handled;
}

int dp_run_panel(void)
{
   struct key_event ke;
   int rc;

   if (!dp_screens_head) {
      fprintf(stderr, "dp: no screens registered\n");
      return 1;
   }

   dp_ctx = dp_screens_head;
   dp_enter();

   memset(&ke, 0, sizeof(ke));
   ui_need_update = true;
   need_chrome = true;
   need_buffer = true;
   need_paint  = true;

   while (1) {

      rc = dp_main_body(ke);

      if (!rc && ke.print_char == 'q')
         break;

      if (tui_read_ke(&ke) < 0)
         break;

      if (ke.print_char == TUI_KEY_CTRL_C)
         break;
   }

   dp_exit();
   return 0;
}
