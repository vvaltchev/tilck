/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Direct-emit terminal primitives — VT100 escape sequences, cursor
 * moves, alt-buffer / cursor-visibility toggles, box drawing. All
 * functions write straight to STDOUT_FILENO; none touch panel state.
 *
 * The matching buffered-emit layer (dp_write / dp_writeln / dp_buf_*)
 * lives in userapps/dp/dp_panel.c — keep these two halves separate so
 * tools that only need direct emit (e.g. the tracer) don't drag in
 * the panel context.
 */

#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

#include "term.h"

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
