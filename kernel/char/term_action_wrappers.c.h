#ifndef _TERM_C_
#error This is NOT a header file. It is part of term.c.
#endif

/* ---------------- term action engine --------------------- */

static const actions_table_item actions_table[] = {
   [a_write] = {(action_func)term_action_write, 3},
   [a_scroll] = {(action_func)term_action_scroll, 1},
   [a_set_col_offset] = {(action_func)term_action_set_col_offset, 1},
   [a_move_ch_and_cur] = {(action_func)term_action_move_ch_and_cur, 2},
   [a_move_ch_and_cur_rel] = {(action_func)term_action_move_ch_and_cur_rel, 2},
   [a_reset] = {(action_func)term_action_reset, 1},
   [a_erase_in_display] = {(action_func)term_action_erase_in_display, 1},
   [a_erase_in_line] = {(action_func)term_action_erase_in_line, 1},
   [a_non_buf_scroll_up] = {(action_func)term_action_non_buf_scroll_up, 1},
   [a_non_buf_scroll_down] = {(action_func)term_action_non_buf_scroll_down, 1},
   [a_pause_video_output] = {(action_func)term_action_pause_video_output, 1},
   [a_restart_video_output] = {(action_func)term_action_restart_video_output, 1}
};

static void term_execute_action(term *t, term_action *a)
{
   ASSERT(a->type3 < ARRAY_SIZE(actions_table));

   const actions_table_item *it = &actions_table[a->type3];

   switch (it->args_count) {
      case 3:
         it->func(t, a->ptr, a->len, a->col);
         break;
      case 2:
         it->func(t, a->arg1, a->arg2);
         break;
      case 1:
         it->func(t, a->arg);
         break;
      default:
         NOT_REACHED();
   }
}


static void term_execute_or_enqueue_action(term *t, term_action a)
{
   bool written;
   bool was_empty;

   written = ringbuf_write_elem_ex(&term_ringbuf, &a, &was_empty);

   /*
    * written would be false only if the ringbuf was full. In order that to
    * happen, we'll need ARRAY_SIZE(term_actions_buf) nested interrupts and
    * all of them need to issue a term_* call. Virtually "impossible".
    */
   VERIFY(written);

   if (was_empty) {

      while (ringbuf_read_elem(&term_ringbuf, &a))
         term_execute_action(t, &a);

   }
}

void term_write(term *t, const char *buf, u32 len, u8 color)
{
   ASSERT(len < MB);

   term_action a = {
      .type3 = a_write,
      .ptr = (uptr)buf,
      .len = MIN(len, (uptr)MB - 1),
      .col = color
   };

   term_execute_or_enqueue_action(t, a);
}

void term_move_ch_and_cur(term *t, u32 row, u32 col)
{
   term_action a = {
      .type2 = a_move_ch_and_cur,
      .arg1 = row,
      .arg2 = col
   };

   term_execute_or_enqueue_action(t, a);
}

void term_scroll_up(term *t, u32 lines)
{
   term_action a = {
      .type1 = a_scroll,
      .arg = (int)lines
   };

   term_execute_or_enqueue_action(t, a);
}

void term_scroll_down(term *t, u32 lines)
{
   term_action a = {
      .type1 = a_scroll,
      .arg = -((int)lines)
   };

   term_execute_or_enqueue_action(t, a);
}

void term_set_col_offset(term *t, u32 off)
{
   term_action a = {
      .type1 = a_set_col_offset,
      .arg = off
   };

   term_execute_or_enqueue_action(t, a);
}

void term_move_ch_and_cur_rel(term *t, s8 dx, s8 dy)
{
   term_action a = {
      .type2 = a_move_ch_and_cur_rel,
      .arg1 = dx,
      .arg2 = dy
   };

   term_execute_or_enqueue_action(t, a);
}

void term_pause_video_output(term *t)
{
   term_action a = {
      .type1 = a_pause_video_output,
      .arg = 0
   };

   term_execute_or_enqueue_action(t, a);
}

void term_restart_video_output(term *t)
{
   term_action a = {
      .type1 = a_restart_video_output,
      .arg = 0
   };

   term_execute_or_enqueue_action(t, a);
}

/* ---------------- term non-action interface funcs --------------------- */

u32 term_get_tab_size(term *t)
{
   return t->tabsize;
}

u32 term_get_rows(term *t)
{
   return t->rows;
}

u32 term_get_cols(term *t)
{
   return t->cols;
}

u32 term_get_curr_row(term *t)
{
   return t->current_row;
}

u32 term_get_curr_col(term *t)
{
   return t->current_col;
}

void term_set_filter_func(term *t, term_filter_func func, void *ctx)
{
   filter = func;
   filter_ctx = ctx;
}

term_filter_func term_get_filter_func(term *t)
{
   return filter;
}

bool term_is_initialized(term *t)
{
   return t->initialized;
}
