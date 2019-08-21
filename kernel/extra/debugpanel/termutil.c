/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/kernel/term.h>
#include "termutil.h"

void dp_write_raw(const char *fmt, ...)
{
   char buf[256];
   va_list args;
   int rc;

   va_start(args, fmt);
   rc = vsnprintk(buf, sizeof(buf), fmt, args);
   va_end(args);

   term_write(get_curr_term(), buf, (size_t)rc, 15);
}

void dp_writeln(const char *fmt, ...)
{
   char buf[256];
   va_list args;
   int rc;

   va_start(args, fmt);
   rc = vsnprintk(buf, sizeof(buf), fmt, args);
   va_end(args);

   dp_move_right(dp_start_col + 1);
   term_write(get_curr_term(), buf, (size_t)rc, 15);
   term_write(get_curr_term(), "\n", 1, 15);
}

void dp_draw_rect(int row, int col, int h, int w)
{
   ASSERT(w >= 2);
   ASSERT(h >= 2);

   dp_write_raw(GFX_ON);
   dp_move_cursor(row, col);
   dp_write_raw("l");

   for (int i = 0; i < w-2; i++) {
      dp_write_raw("q");
   }

   dp_write_raw("k");

   for (int i = 1; i < h-1; i++) {

      dp_move_cursor(row+i, col);
      dp_write_raw("x");

      dp_move_cursor(row+i, col+w-1);
      dp_write_raw("x");
   }

   dp_move_cursor(row+h-1, col);
   dp_write_raw("m");

   for (int i = 0; i < w-2; i++) {
      dp_write_raw("q");
   }

   dp_write_raw("j");
   dp_write_raw(GFX_OFF);
}
