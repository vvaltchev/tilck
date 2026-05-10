/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * VT100 primitives split into two layers:
 *
 *   Direct-emit (dp_write_raw, dp_move_cursor, dp_clear, dp_draw_rect_raw,
 *   dp_show_modal_msg, …) — write to STDOUT_FILENO immediately. Used by
 *   the chrome (border, panel-tabs header, footer, modal overlays) and
 *   by terminal-lifecycle helpers.
 *
 *   Buffered-emit (dp_write / dp_writeln / dp_writeln2) — append to an
 *   in-memory line buffer keyed by panel-local row. Used by every panel's
 *   draw_func. After draw_func returns, the buffer is painted onto the
 *   panel content area by dp_buf_paint(); the chrome is preserved across
 *   the paint, so scrolling does not flicker the border or re-issue the
 *   panel-tabs header.
 *
 * The buffer is cleared by dp_buf_reset() before each call to the active
 * panel's draw_func. PAGE_UP/PAGE_DOWN scrolling repaints the same
 * buffer at a different row_off (no re-execution of draw_func, no
 * re-issuing of any data syscalls).
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "termutil.h"
#include "dp_int.h"

/* ------------------------- direct-emit layer ------------------------- */

void dp_write_raw_int(const char *buf, int len)
{
   ssize_t off = 0;

   while (off < len) {

      ssize_t rc = write(STDOUT_FILENO, buf + off, (size_t)(len - off));

      if (rc < 0)
         return;       /* give up; nothing useful to report */

      off += rc;
   }
}

void dp_write_raw(const char *fmt, ...)
{
   char buf[256];
   va_list args;
   int rc;

   va_start(args, fmt);
   rc = vsnprintf(buf, sizeof(buf), fmt, args);
   va_end(args);

   if (rc <= 0)
      return;

   if ((size_t)rc >= sizeof(buf))
      rc = (int)sizeof(buf) - 1;

   dp_write_raw_int(buf, rc);
}

void dp_move_right(int n)         { dp_write_raw("\033[%dC", n); }
void dp_move_left(int n)          { dp_write_raw("\033[%dD", n); }
void dp_move_to_col(int n)        { dp_write_raw("\033[%dG", n); }
void dp_clear(void)               { dp_write_raw(ERASE_DISPLAY); }
void dp_move_cursor(int row, int col) { dp_write_raw("\033[%d;%dH", row, col); }

void dp_set_cursor_enabled(bool enabled)
{
   dp_write_raw("%s", enabled ? SHOW_CURSOR : HIDE_CURSOR);
}

void dp_switch_to_alt_buffer(void)
{
   dp_write_raw("%s", USE_ALT_BUF);
}

void dp_switch_to_default_buffer(void)
{
   dp_write_raw("%s", USE_DEF_BUF);
}

void dp_draw_rect_raw(int row, int col, int h, int w)
{
   if (w < 2 || h < 2)
      return;

   dp_write_raw(GFX_ON);
   dp_move_cursor(row, col);
   dp_write_raw("l");

   for (int i = 0; i < w-2; i++)
      dp_write_raw("q");

   dp_write_raw("k");

   for (int i = 1; i < h-1; i++) {

      dp_move_cursor(row+i, col);
      dp_write_raw("x");

      dp_move_cursor(row+i, col+w-1);
      dp_write_raw("x");
   }

   dp_move_cursor(row+h-1, col);
   dp_write_raw("m");

   for (int i = 0; i < w-2; i++)
      dp_write_raw("q");

   dp_write_raw("j");
   dp_write_raw(GFX_OFF);
}

/*
 * Direct-emit version of dp_draw_rect: same shape as dp_draw_rect_raw
 * but with an optional label. Used by the modal overlay (which paints
 * outside the buffered panel content) and by anyone wanting a labeled
 * rectangle drawn straight to the TTY.
 */
void dp_draw_rect(const char *label,
                  const char *esc_label_color,
                  int row,
                  int col,
                  int h,
                  int w)
{
   dp_draw_rect_raw(row, col, h, w);

   if (label) {
      dp_move_cursor(row, col + 2);
      dp_write_raw("%s[ %s ]" RESET_ATTRS, esc_label_color, label);
   }
}

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
