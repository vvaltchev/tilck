/* SPDX-License-Identifier: BSD-2-Clause */

typedef void (*action_func)(struct vterm *, ulong, ulong, ulong);

struct actions_table_item {

   action_func func;
   u32 action_kind;
};

#define ENTRY(func, n) { (void *)(__term_action_##func), n }

static const struct actions_table_item actions_table[] = {
   [a_none]                 = ENTRY(none, 0),
   [a_write]                = ENTRY(write, 3),
   [a_direct_write]         = ENTRY(direct_write, 3),
   [a_del_generic]          = ENTRY(del, 2),
   [a_scroll]               = ENTRY(scroll, 2),
   [a_set_col_offset]       = ENTRY(set_col_offset, 1),
   [a_move_cur]             = ENTRY(move_cur, 4),
   [a_move_cur_rel]         = ENTRY(move_cur_rel, 4),
   [a_reset]                = ENTRY(reset, 0),
   [a_pause_output]         = ENTRY(pause_output, 0),
   [a_restart_output]       = ENTRY(restart_output, 0),
   [a_enable_cursor]        = ENTRY(enable_cursor, 1),
   [a_non_buf_scroll]       = ENTRY(non_buf_scroll, 2),
   [a_use_alt_buffer]       = ENTRY(use_alt_buffer, 1),
   [a_insert_blank_lines]   = ENTRY(ins_blank_lines, 1),
   [a_delete_lines]         = ENTRY(del_lines, 1),
   [a_set_scroll_region]    = ENTRY(set_scroll_region, 2),
   [a_insert_blank_chars]   = ENTRY(ins_blank_chars, 1),
   [a_simple_del_chars]     = ENTRY(del_chars_in_line, 1),
   [a_simple_erase_chars]   = ENTRY(erase_chars_in_line, 1),
};

#undef ENTRY

static void term_execute_action(struct vterm *t, struct term_action *a)
{
   ASSERT(a->type3 < ARRAY_SIZE(actions_table));

   const struct actions_table_item *it = &actions_table[a->type3];

   switch (it->action_kind) {
      case 0:
         CALL_ACTION_FUNC_0(it->func, t);
         break;
      case 1:
         CALL_ACTION_FUNC_1(it->func, t, a->arg);
         break;
      case 2:
         CALL_ACTION_FUNC_2(it->func, t, a->arg1, a->arg2);
         break;
      case 3:
         CALL_ACTION_FUNC_3(it->func, t, a->ptr, a->len, a->col);
         break;
      case 4:
         CALL_ACTION_FUNC_2(it->func, t, a->arg16_1, a->arg16_2);
         break;
      default:
         NOT_REACHED();
   }
}

static void
term_execute_or_enqueue_action(struct vterm *t, struct term_action *a)
{
   term_execute_or_enqueue_action_template(t,
                                           &t->rb_data,
                                           a,
                                           (void *)&term_execute_action);
}

static void
vterm_write(term *t, const char *buf, size_t len, u8 color)
{
   struct term_action a;
   ASSERT(len < MB);

   if (in_panic()) {
      term_action_write(t, buf, (u32)len, color);
      return;
   }

   term_make_action_write(&a, buf, (u32)len, color);
   term_execute_or_enqueue_action(t, &a);
}

static void
vterm_scroll_up(term *t, u32 rows)
{
   struct term_action a;
   term_make_action_scroll(&a, term_scroll_up, rows);
   term_execute_or_enqueue_action(t, &a);
}

static void
vterm_scroll_down(term *t, u32 rows)
{
   struct term_action a;
   term_make_action_scroll(&a, term_scroll_down, rows);
   term_execute_or_enqueue_action(t, &a);
}

static void
vterm_set_col_offset(term *_t, int off)
{
   struct vterm *const t = _t;
   struct term_action a;
   term_make_action_set_col_off(&a, off >= 0 ? (u32)off : t->c);
   term_execute_or_enqueue_action(t, &a);
}

static void
vterm_pause_output(term *t)
{
   struct term_action a;
   term_make_action_pause_output(&a);
   term_execute_or_enqueue_action(t, &a);
}

static void
vterm_restart_output(term *t)
{
   struct term_action a;
   term_make_action_restart_output(&a);
   term_execute_or_enqueue_action(t, &a);
}

/* ---------------- term non-action interface funcs --------------------- */

u16 vterm_get_curr_row(struct vterm *t)
{
   return t->r;
}

u16 vterm_get_curr_col(struct vterm *t)
{
   return t->c;
}

static void
vterm_set_filter(term *_t, term_filter func, void *ctx)
{
   struct vterm *const t = _t;
   t->filter = func;
   t->filter_ctx = ctx;
}

static bool
vterm_is_initialized(term *_t)
{
   struct vterm *const t = _t;
   return t->initialized;
}
