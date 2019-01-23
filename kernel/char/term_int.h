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

typedef void (*action_func)(/* unspecified number of arguments */);

typedef struct {

   action_func func;
   u32 args_count;

} actions_table_item;

void term_internal_write_char2(char c, u8 color);
void term_internal_write_backspace(u8 color);
