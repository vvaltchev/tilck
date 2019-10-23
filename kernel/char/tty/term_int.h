/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

enum term_non_buf_scroll_type {
   non_buf_scroll_up,
   non_buf_scroll_down
};

enum term_action_type {

   a_none,
   a_write,
   a_dwrite_no_filter,   /* direct write w/o filters/scroll/move_cursor/flush */
   a_del_generic,
   a_scroll,               /* arg1 = rows, arg2 = direction */
   a_set_col_offset,
   a_move_ch_and_cur,
   a_move_ch_and_cur_rel,
   a_reset,
   a_pause_video_output,
   a_restart_video_output,
   a_enable_cursor,
   a_use_alt_buffer,
   a_non_buf_scroll,   /*
                        * non_buf scroll: arg1 = rows, arg2 = direction
                        * up => text moves up => new blank lines at the bottom
                        * down => text moves down => new blank lines at the top
                        */
};

typedef void (*action_func)(term *t, ...);

struct actions_table_item {

   action_func func;
   u32 args_count;
};

enum term_del_type {

   TERM_DEL_PREV_CHAR,
   TERM_DEL_PREV_WORD,
   TERM_DEL_ERASE_IN_DISPLAY,
   TERM_DEL_ERASE_IN_LINE,
};

/* --- term write filter interface --- */

enum term_fret {
   TERM_FILTER_WRITE_BLANK,
   TERM_FILTER_WRITE_C,
};

struct term_action {

   union {

      struct {
         u32 type3 :  4;
         u32 len   : 20;
         u32 col   :  8;
      };

      struct {
         u32 type2 :  4;
         u32 arg1  : 14;
         u32 arg2  : 14;
      };

      struct {
         u32 type1  :  4;
         u32 arg    : 28;
      };

   };

   uptr ptr;
};

STATIC_ASSERT(sizeof(struct term_action) == (2 * sizeof(uptr)));

typedef enum term_fret (*term_filter)(u8 *c,                 /* in/out */
                                      u8 *color,             /* in/out */
                                      struct term_action *a, /*  out   */
                                      void *ctx);            /*   in   */

void term_set_filter(term *t, term_filter func, void *ctx);
term_filter term_get_filter(term *t);

/* --- */

term *alloc_term_struct(void);
void free_term_struct(term *t);
void dispose_term(term *t);

void set_curr_term(term *t);
