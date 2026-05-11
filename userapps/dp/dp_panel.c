/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Panel context + buffered-emit + modal overlay — the panel-aware
 * bits that used to live mixed inside dp/dp_screen.c and
 * dp/termutil.c. Lives in the dp binary only; the tracer (and any
 * future tool that only uses the direct-emit layer in common/term.h)
 * does not link this file.
 *
 * Three pieces:
 *
 *   1. The "current panel" pointer dp_ctx and its default backing
 *      struct dp_default_ctx, plus the small ui_need_update / modal_msg
 *      globals consulted by dp_main's main loop.
 *
 *   2. dp_write / dp_writeln / dp_buf_reset / dp_buf_paint — the
 *      row-keyed in-memory buffer panels write into during their
 *      draw_func; dp_main repaints from this buffer at a given
 *      row_off for free scrolling without re-running draw_func.
 *
 *   3. dp_show_modal_msg — centered alert overlay; paints straight
 *      to the TTY (direct-emit), so callers must trigger a redraw
 *      to bring the panel content back underneath.
 */

#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "term.h"
#include "tui_layout.h"
#include "dp_int.h"
#include "dp_panel.h"

/* --------------------------- panel context --------------------------- */

bool ui_need_update;
const char *modal_msg;

/*
 * Default panel context. row_max is set wide so dp_write's clipping
 * never trims a write before any real screen has been pushed. The
 * registry in dp_main.c swaps dp_ctx out for an actual struct dp_screen
 * on each panel switch.
 */
static struct dp_screen dp_default_ctx = {
   .row_max = INT_MAX,
};

struct dp_screen *dp_ctx = &dp_default_ctx;

/* ------------------------ buffered-emit layer ------------------------ */

#define DP_BUF_MAX_LINES   256
#define DP_BUF_MAX_CHUNKS  4
#define DP_BUF_CHUNK_SZ    256

struct dp_buf_chunk {
   int  col;                            /* terminal column, 0 = default */
   int  text_len;
   char text[DP_BUF_CHUNK_SZ];
};

struct dp_buf_line {
   int chunk_count;
   struct dp_buf_chunk chunks[DP_BUF_MAX_CHUNKS];
};

static struct dp_buf_line dp_buf[DP_BUF_MAX_LINES];
static int dp_buf_lines;            /* highest relrow + 1 ever filled */

void dp_buf_reset(void)
{
   for (int i = 0; i < dp_buf_lines; i++)
      dp_buf[i].chunk_count = 0;

   dp_buf_lines = 0;
}

static void
dp_buf_append(int relrow, int col, const char *text, int text_len)
{
   if (relrow < 0 || relrow >= DP_BUF_MAX_LINES)
      return;

   if (relrow >= dp_buf_lines)
      dp_buf_lines = relrow + 1;

   struct dp_buf_line *L = &dp_buf[relrow];

   if (L->chunk_count >= DP_BUF_MAX_CHUNKS)
      return;

   struct dp_buf_chunk *C = &L->chunks[L->chunk_count++];

   C->col = col;

   if (text_len >= DP_BUF_CHUNK_SZ)
      text_len = DP_BUF_CHUNK_SZ - 1;

   C->text_len = text_len;
   memcpy(C->text, text, (size_t)text_len);
}

void dp_write(int row, int col, const char *fmt, ...)
{
   char buf[DP_BUF_CHUNK_SZ];
   va_list args;
   int rc;
   const int relrow = row - dp_screen_start_row;

   if (relrow < 0)
      return;        /* above the panel area; not addressable */

   if (relrow > dp_ctx->row_max)
      dp_ctx->row_max = relrow;

   va_start(args, fmt);
   rc = vsnprintf(buf, sizeof(buf), fmt, args);
   va_end(args);

   if (rc <= 0)
      return;

   if ((size_t)rc >= sizeof(buf))
      rc = (int)sizeof(buf) - 1;

   dp_buf_append(relrow, col, buf, rc);
}

/*
 * Paint dp_buf onto the terminal. Splits the panel content area into
 * two regions:
 *
 *   Static region — panel rows [0, static_rows). These map directly
 *   onto buffer rows [0, static_rows) regardless of row_off, so any
 *   "header" content the screen wrote into the first few rows stays
 *   pinned at the top while the user scrolls.
 *
 *   Scrollable region — panel rows [static_rows, screen_rows). These
 *   map onto buffer rows [static_rows + row_off, …]. row_off advances
 *   the scrollable view but leaves the static region alone.
 *
 *   Default static_rows == 0 means "everything scrolls" — the original
 *   behavior, used by every screen that doesn't opt in.
 *
 * Each line is cleared with panel_w-2 spaces before its chunks are
 * emitted (preserves the right border, which `\033[K` would wipe).
 */
void dp_buf_paint(int row_off,
                  int screen_rows,
                  int term_first_row,
                  int panel_left_col,
                  int panel_w,
                  int static_rows)
{
   char spaces[DP_W];
   const int clear_len = panel_w - 2 < (int)sizeof(spaces)
                         ? panel_w - 2
                         : (int)sizeof(spaces);
   memset(spaces, ' ', (size_t)clear_len);

   for (int i = 0; i < screen_rows; i++) {

      const int term_row = term_first_row + i;
      const int relrow = (i < static_rows) ? i : i + row_off;

      /* Position to start of content area and overwrite with spaces. */
      dp_move_cursor(term_row, panel_left_col + 1);
      dp_write_raw_int(spaces, clear_len);

      if (relrow >= dp_buf_lines)
         continue;       /* row already cleared, no chunks to emit */

      const struct dp_buf_line *L = &dp_buf[relrow];

      for (int c = 0; c < L->chunk_count; c++) {

         const struct dp_buf_chunk *C = &L->chunks[c];
         const int term_col = (C->col == 0) ? panel_left_col + 2 : C->col;

         dp_move_cursor(term_row, term_col);
         dp_write_raw_int(C->text, C->text_len);
      }
   }
}

/* ----------------------- modal overlay (direct) ---------------------- */

void dp_show_modal_msg(const char *msg)
{
   static const char common_msg[] = "Press ANY key to continue";
   const int max_line_len = DP_W - 2 - 2 - 2;
   const int msg_len = (int)strlen(msg);

   int row_len = msg_len < max_line_len ? msg_len : max_line_len;
   if (row_len < (int)sizeof(common_msg) - 1)
      row_len = (int)sizeof(common_msg) - 1;
   row_len += 2;

   const int srow = dp_start_row + DP_H / 2 - 5 / 2;
   const int scol = dp_cols / 2 - row_len / 2;

   char clear_buf[DP_W+1];

   if (msg_len > max_line_len)
      return; /* for the moment, no multi-line */

   memset(clear_buf, ' ', sizeof(clear_buf) - 1);
   clear_buf[row_len] = 0;

   /* Clear the rectangle area first (overwrite whatever was there). */
   for (int i = 0; i < 3; i++) {
      dp_move_cursor(srow + i, scol);
      dp_write_raw("%s", clear_buf);
   }

   /* Draw the alert rectangle border + label. */
   dp_draw_rect("Alert", E_COLOR_BR_RED,
                srow - 1, scol - 1, 5, row_len + 2);

   /* The message itself. */
   dp_move_cursor(srow, scol);
   dp_write_raw(" %s", msg);

   /* The "Press ANY key" hint. */
   dp_move_cursor(srow + 2,
                  dp_cols / 2 - ((int)sizeof(common_msg)-1) / 2);
   dp_write_raw(E_COLOR_YELLOW "%s" RESET_ATTRS, common_msg);
}
