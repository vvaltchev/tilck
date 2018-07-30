
#pragma once
#include <tilck/common/basic_defs.h>

typedef enum {

   a_none,
   a_write,
   a_scroll_up,
   a_scroll_down,
   a_set_col_offset,
   a_move_ch_and_cur,
   a_move_ch_and_cur_rel,
   a_reset,
   a_erase_in_display

} term_action_type;

typedef void (*action_func)();

typedef struct {

   action_func func;
   u32 args_count;

} actions_table_item;

void term_internal_write_char2(char c, u8 color);

