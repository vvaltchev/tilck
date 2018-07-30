
#pragma once
#include <tilck/common/basic_defs.h>

typedef enum {

   a_none,
   a_write,
   a_scroll_up,          /* text moves DOWN => old text from buf at the top  */
   a_scroll_down,        /* text moves UP => old text from buf at the bottom */
   a_set_col_offset,
   a_move_ch_and_cur,
   a_move_ch_and_cur_rel,
   a_reset,
   a_erase_in_display,
   a_erase_in_line,
   a_non_buf_scroll_up,   /* text moves up => new blank lines at the bottom  */
   a_non_buf_scroll_down  /* text moves down => new blank lines at the top   */

} term_action_type;

typedef void (*action_func)();

typedef struct {

   action_func func;
   u32 args_count;

} actions_table_item;

void term_internal_write_char2(char c, u8 color);

