#ifndef _TERM_C_
#error This is NOT a header file. It is part of term.c.
#endif

/* ---------------- term action engine --------------------- */

#define ENTRY(func, n) { (action_func)(func), n }

static const struct actions_table_item actions_table[] = {
   [a_write]                = ENTRY(term_action_write, 3),
   [a_dwrite_no_filter]     = ENTRY(term_action_dwrite_no_filter, 3),
   [a_del_generic]          = ENTRY(term_action_del, 2),
   [a_scroll]               = ENTRY(term_action_scroll, 2),
   [a_set_col_offset]       = ENTRY(term_action_set_col_offset, 1),
   [a_move_ch_and_cur]      = ENTRY(term_action_move_ch_and_cur, 2),
   [a_move_ch_and_cur_rel]  = ENTRY(term_action_move_ch_and_cur_rel, 2),
   [a_reset]                = ENTRY(term_action_reset, 1),
   [a_pause_video_output]   = ENTRY(term_action_pause_video_output, 1),
   [a_restart_video_output] = ENTRY(term_action_restart_video_output, 1),
   [a_enable_cursor]        = ENTRY(term_action_enable_cursor, 1),
   [a_non_buf_scroll]       = ENTRY(term_action_non_buf_scroll, 2),
   [a_use_alt_buffer]       = ENTRY(term_action_use_alt_buffer, 1),
};

#undef ENTRY

static void term_execute_action(term *t, struct term_action *a)
{
   ASSERT(a->type3 < ARRAY_SIZE(actions_table));

   const struct actions_table_item *it = &actions_table[a->type3];

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


static void term_execute_or_enqueue_action(term *t, struct term_action a)
{
   bool written;
   bool was_empty;

   written = safe_ringbuf_write_elem_ex(&t->ringb, &a, &was_empty);

   /*
    * written would be false only if the ringbuf was full. In order that to
    * happen, we'll need ARRAY_SIZE(actions_buf) nested interrupts and
    * all of them need to issue a term_* call. Virtually "impossible".
    */
   VERIFY(written);

   if (was_empty) {

      while (safe_ringbuf_read_elem(&t->ringb, &a))
         term_execute_action(t, &a);

   }
}

void term_write(term *t, const char *buf, size_t len, u8 color)
{
   ASSERT(len < MB);

   struct term_action a = {

      .type3 = a_write,
      .len = UNSAFE_MIN((u32)len, (u32)MB - 1),
      .col = color,
      .ptr = (uptr)buf,
   };

   term_execute_or_enqueue_action(t, a);
}

void term_scroll_up(term *t, u32 lines)
{
   struct term_action a = {
      .type2 = a_scroll,
      .arg1 = lines,
      .arg2 = 0,
   };

   term_execute_or_enqueue_action(t, a);
}

void term_scroll_down(term *t, u32 lines)
{
   struct term_action a = {
      .type2 = a_scroll,
      .arg1 = lines,
      .arg2 = 1,
   };

   term_execute_or_enqueue_action(t, a);
}

void term_set_col_offset(term *t, u32 off)
{
   struct term_action a = {
      .type1 = a_set_col_offset,
      .arg = off,
   };

   term_execute_or_enqueue_action(t, a);
}

void term_pause_video_output(term *t)
{
   struct term_action a = {
      .type1 = a_pause_video_output,
      .arg = 0,
   };

   term_execute_or_enqueue_action(t, a);
}

void term_restart_video_output(term *t)
{
   struct term_action a = {
      .type1 = a_restart_video_output,
      .arg = 0,
   };

   term_execute_or_enqueue_action(t, a);
}

void term_set_cursor_enabled(term *t, bool value)
{
   struct term_action a = {
      .type1 = a_enable_cursor,
      .arg = value,
   };

   term_execute_or_enqueue_action(t, a);
}

/* ---------------- term non-action interface funcs --------------------- */

u16 term_get_tab_size(term *t)
{
   return t->tabsize;
}

u16 term_get_rows(term *t)
{
   return t->rows;
}

u16 term_get_cols(term *t)
{
   return t->cols;
}

u16 term_get_curr_row(term *t)
{
   return t->r;
}

u16 term_get_curr_col(term *t)
{
   return t->c;
}

void term_set_filter(term *t, term_filter func, void *ctx)
{
   t->filter = func;
   t->filter_ctx = ctx;
}

term_filter term_get_filter(term *t)
{
   return t->filter;
}

bool term_is_initialized(term *t)
{
   return t->initialized;
}
