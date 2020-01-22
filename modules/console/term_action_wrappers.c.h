/* ---------------- term action engine --------------------- */

struct actions_table_item {

   action_func func;
   u32 args_count;
};

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

static void term_execute_action(struct term *t, struct term_action *a)
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


static void term_execute_or_enqueue_action(struct term *t, struct term_action a)
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

static void
vterm_write(struct term *t, const char *buf, size_t len, u8 color)
{
   struct term_action a;
   ASSERT(len < MB);

   term_make_action_write(&a, buf, len, color);
   term_execute_or_enqueue_action(t, a);
}

static void
vterm_scroll_up(struct term *t, u32 rows)
{
   struct term_action a;
   term_make_action_scroll(&a, term_scroll_up, rows);
   term_execute_or_enqueue_action(t, a);
}

static void
vterm_scroll_down(struct term *t, u32 rows)
{
   struct term_action a;
   term_make_action_scroll(&a, term_scroll_down, rows);
   term_execute_or_enqueue_action(t, a);
}

static void
vterm_set_col_offset(struct term *t, int off)
{
   struct term_action a;
   term_make_action_set_col_off(&a, off >= 0 ? (u32)off : t->c);
   term_execute_or_enqueue_action(t, a);
}

static void
vterm_pause_video_output(struct term *t)
{
   struct term_action a = {
      .type1 = a_pause_video_output,
      .arg = 0,
   };

   term_execute_or_enqueue_action(t, a);
}

static void
vterm_restart_video_output(struct term *t)
{
   struct term_action a = {
      .type1 = a_restart_video_output,
      .arg = 0,
   };

   term_execute_or_enqueue_action(t, a);
}

/* ---------------- term non-action interface funcs --------------------- */

u16 term_get_curr_row(struct term *t)
{
   return t->r;
}

u16 term_get_curr_col(struct term *t)
{
   return t->c;
}

static void
vterm_set_filter(struct term *t, term_filter func, void *ctx)
{
   t->filter = func;
   t->filter_ctx = ctx;
}

static bool
vterm_is_initialized(struct term *t)
{
   return t->initialized;
}
