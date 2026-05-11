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

void term_write_n(const char *buf, int len)
{
   ssize_t off = 0;

   while (off < len) {

      ssize_t rc = write(STDOUT_FILENO, buf + off, (size_t)(len - off));

      if (rc < 0)
         return;       /* give up; nothing useful to report */

      off += rc;
   }
}

void term_write(const char *fmt, ...)
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

   term_write_n(buf, rc);
}

void term_move_right(int n)         { term_write("\033[%dC", n); }
void term_move_left(int n)          { term_write("\033[%dD", n); }
void term_move_to_col(int n)        { term_write("\033[%dG", n); }
void term_clear(void)               { term_write(ERASE_DISPLAY); }
void term_move_cursor(int row, int col) { term_write("\033[%d;%dH", row, col); }

void term_cursor_enable(bool enabled)
{
   term_write("%s", enabled ? SHOW_CURSOR : HIDE_CURSOR);
}

void term_alt_buffer_enter(void)
{
   term_write("%s", USE_ALT_BUF);
}

void term_alt_buffer_exit(void)
{
   term_write("%s", USE_DEF_BUF);
}

void term_draw_rect_raw(int row, int col, int h, int w)
{
   if (w < 2 || h < 2)
      return;

   term_write(GFX_ON);
   term_move_cursor(row, col);
   term_write("l");

   for (int i = 0; i < w-2; i++)
      term_write("q");

   term_write("k");

   for (int i = 1; i < h-1; i++) {

      term_move_cursor(row+i, col);
      term_write("x");

      term_move_cursor(row+i, col+w-1);
      term_write("x");
   }

   term_move_cursor(row+h-1, col);
   term_write("m");

   for (int i = 0; i < w-2; i++)
      term_write("q");

   term_write("j");
   term_write(GFX_OFF);
}

void term_draw_rect_labeled(const char *label,
                  const char *esc_label_color,
                  int row,
                  int col,
                  int h,
                  int w)
{
   term_draw_rect_raw(row, col, h, w);

   if (label) {
      term_move_cursor(row, col + 2);
      term_write("%s[ %s ]" RESET_ATTRS, esc_label_color, label);
   }
}
