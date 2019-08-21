/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/kernel/kb.h>
#include <tilck/kernel/list.h>

#define DP_W   76
#define DP_H   23

typedef struct {

   list_node node;

   int index;
   int row_off;
   int row_max;
   const char *label;
   void (*draw_func)(void);
   keypress_func on_keypress_func;

} dp_screen;

extern int dp_rows;
extern int dp_cols;
extern int dp_start_row;
extern int dp_end_row;
extern int dp_start_col;
extern int dp_screen_start_row;
extern int dp_screen_rows_count;
extern bool ui_need_update;
extern dp_screen *dp_ctx;

static inline sptr dp_int_abs(sptr val) {
   return val >= 0 ? val : -val;
}

void dp_register_screen(dp_screen *screen);
