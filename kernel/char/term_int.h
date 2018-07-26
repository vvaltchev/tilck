
#pragma once
#include <tilck/common/basic_defs.h>

typedef enum {

   a_write,
   a_scroll_up,
   a_scroll_down,
   a_set_col_offset,
   a_move_ch_and_cur,
   a_move_ch_and_cur_rel

} term_action_type;

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

typedef void (*action_func)();

typedef struct {

   action_func func;
   u32 args_count;

} actions_table_item;

void term_execute_action(term_action *a);
void term_internal_write_char2(char c, u8 color);
