/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

enum term_action {

   a_none,
   a_write,
   a_scroll,               /* > 0 scrollup: text moves DOWN; < 0 the opposite */
   a_set_col_offset,
   a_move_ch_and_cur,
   a_move_ch_and_cur_rel,
   a_reset,
   a_erase_in_display,
   a_erase_in_line,
   a_non_buf_scroll_up,   /* text moves up => new blank lines at the bottom  */
   a_non_buf_scroll_down, /* text moves down => new blank lines at the top   */
   a_pause_video_output,
   a_restart_video_output

};

typedef void (*action_func)(term *t, ...);

typedef struct {

   action_func func;
   u32 args_count;

} actions_table_item;

/* --- term write filter interface --- */

enum term_fret {
   TERM_FILTER_WRITE_BLANK,
   TERM_FILTER_WRITE_C
};

typedef struct {

   union {

      struct {
         u64 type3 :  4;
         u64 len   : 20;
         u64 col   :  8;
         u64 ptr   : 32;
      };

      struct {
         u64 type2 :  4;
         u64 arg1  : 30;
         u64 arg2  : 30;
      };

      struct {
         u64 type1  :  4;
         u64 arg    : 32;
         u64 unused : 28;
      };

      u64 raw;
   };

} term_action;

typedef enum term_fret (*term_filter)(u8 c,
                                      u8 *color /* in/out */,
                                      term_action *a /* out */,
                                      void *ctx);

void term_set_filter_func(term *t, term_filter func, void *ctx);
term_filter term_get_filter_func(term *t);

/* --- */

term *allocate_new_term(void);

void term_internal_write_char2(term *t, char c, u8 color);
void term_internal_write_backspace(term *t, u8 color);
void set_curr_term(term *t);
