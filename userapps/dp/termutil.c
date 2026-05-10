/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Userspace VT100 primitives. Mirrors the kernel debugpanel termutil.c
 * one-to-one, except output goes to STDOUT_FILENO directly. The
 * kernel's `rev_colors` switch (which special-cased video terminals to
 * skip the REVERSE_VIDEO escape) is gone — userspace always emits the
 * escape sequences and lets the terminal do the work.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "termutil.h"
#include "dp_int.h"

void dp_write_raw_int(const char *buf, int len)
{
   ssize_t off = 0;

   while (off < len) {

      ssize_t rc = write(STDOUT_FILENO, buf + off, (size_t)(len - off));

      if (rc < 0)
         return;       /* give up on the write; nothing useful to report */

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

void dp_reverse_colors(void)
{
   dp_write_raw("%s", REVERSE_VIDEO);
}

void dp_reset_attrs(void)
{
   dp_write_raw("%s", RESET_ATTRS);
}

void dp_move_right(int n)
{
   dp_write_raw("\033[%dC", n);
}

void dp_move_left(int n)
{
   dp_write_raw("\033[%dD", n);
}

void dp_move_to_col(int n)
{
   dp_write_raw("\033[%dG", n);
}

void dp_clear(void)
{
   dp_write_raw(ERASE_DISPLAY);
}

void dp_move_cursor(int row, int col)
{
   dp_write_raw("\033[%d;%dH", row, col);
}

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

void dp_write(int row, int col, const char *fmt, ...)
{
   char buf[256];
   va_list args;
   int rc;
   const int relrow = row - dp_screen_start_row;

   if (relrow > dp_ctx->row_max)
      dp_ctx->row_max = relrow;

   if (relrow < dp_ctx->row_off)
      return;

   row -= dp_ctx->row_off;

   if (row > dp_end_row - 2)
      return;

   va_start(args, fmt);
   rc = vsnprintf(buf, sizeof(buf), fmt, args);
   va_end(args);

   if (rc <= 0)
      return;

   if ((size_t)rc >= sizeof(buf))
      rc = (int)sizeof(buf) - 1;

   if (col == 0)
      col = dp_start_col + 2;

   dp_move_cursor(row, col);
   dp_write_raw_int(buf, rc);
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

void dp_draw_rect(const char *label,
                  const char *esc_label_color,
                  int row,
                  int col,
                  int h,
                  int w)
{
   if (w < 2 || h < 2)
      return;

   dp_write_raw(GFX_ON);
   dp_write(row, col, "l");

   for (int i = 0; i <= w-2; i++)
      dp_write(row, col+i+1, "q");

   dp_write(row, col+w-2+1, "k");

   for (int i = 1; i < h-1; i++) {
      dp_write(row+i, col, "x");
      dp_write(row+i, col+w-1, "x");
   }

   dp_write(row+h-1, col, "m");

   for (int i = 0; i <= w-2; i++)
      dp_write(row+h-1, col+i+1, "q");

   dp_write(row+h-1, col+w-2+1, "j");
   dp_write_raw(GFX_OFF);

   if (label)
      dp_write(row, col + 2, "%s[ %s ]" RESET_ATTRS, esc_label_color, label);
}

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

   char buf[DP_W+1];

   if (msg_len > max_line_len)
      return; /* for the moment, no multi-line */

   memset(buf, ' ', sizeof(buf) - 1);
   buf[row_len] = 0;

   /* Clear the area around the message box */
   for (int i = 0; i < 3; i++)
      dp_write(srow + i, scol, "%s", buf);

   /* Draw the rect */
   dp_draw_rect("Alert", E_COLOR_BR_RED, srow - 1, scol - 1, 5, row_len + 2);

   /* Draw the actual alert message */
   dp_write(srow, scol, " %s", msg);

   dp_write(srow + 2,
            dp_cols / 2 - ((int)sizeof(common_msg)-1) / 2,
            E_COLOR_YELLOW "%s" RESET_ATTRS,
            common_msg);
}
