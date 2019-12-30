/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/kernel/kb.h>
#include <tilck/kernel/list.h>
#include <tilck/kernel/fs/vfs.h>

#define DP_W   76
#define DP_H   23

typedef enum kb_handler_action (*dp_keypress_func)(struct key_event);

struct dp_screen {

   struct list_node node;

   int index;
   int row_off;
   int row_max;
   const char *label;
   void (*first_setup)(void);
   void (*draw_func)(void);
   void (*on_dp_enter)(void);
   void (*on_dp_exit)(void);
   dp_keypress_func on_keypress_func;
};

extern int dp_rows;
extern int dp_cols;
extern int dp_start_row;
extern int dp_end_row;
extern int dp_start_col;
extern int dp_screen_start_row;
extern int dp_screen_rows;
extern bool ui_need_update;
extern const char *modal_msg;
extern struct dp_screen *dp_ctx;
extern fs_handle dp_input_handle;

static inline sptr dp_int_abs(sptr val) {
   return val >= 0 ? val : -val;
}

void dp_register_screen(struct dp_screen *screen);
int dp_read_ke_from_tty(struct key_event *ke);
void dp_set_input_blocking(bool blocking);
