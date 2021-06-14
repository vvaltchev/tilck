/* SPDX-License-Identifier: BSD-2-Clause */

#define DEFINE_TERM_ACTION_0(name)                                          \
   static void                                                              \
   __term_action_##name(term *_t, ulong __u1, ulong __u2, ulong __u3) {     \
      struct vterm *const t = _t;                                           \
      term_action_##name(t);                                                \
   }

#define DEFINE_TERM_ACTION_1(name, t1)                                      \
   static void                                                              \
   __term_action_##name(term *_t, ulong __a1, ulong __u1, ulong __u2) {     \
      struct vterm *const t = _t;                                           \
      term_action_##name(t, (t1)__a1);                                      \
   }

#define DEFINE_TERM_ACTION_2(name, t1, t2)                                  \
   static void                                                              \
   __term_action_##name(term *_t, ulong __a1, ulong __a2, ulong __u1) {     \
      struct vterm *const t = _t;                                           \
      term_action_##name(t, (t1)__a1, (t2)__a2);                            \
   }

#define DEFINE_TERM_ACTION_3(name, t1, t2, t3)                              \
   static void                                                              \
   __term_action_##name(term *_t, ulong __a1, ulong __a2, ulong __a3) {     \
      struct vterm *const t = _t;                                           \
      term_action_##name(t, (t1)__a1, (t2)__a2, (t3)__a3);                  \
   }

#define CALL_ACTION_FUNC_0(func, t)                                \
   (func)((t), 0, 0, 0)

#define CALL_ACTION_FUNC_1(func, t, a1)                            \
   (func)((t), (ulong)(a1), 0, 0)

#define CALL_ACTION_FUNC_2(func, t, a1, a2)                        \
   (func)((t), (ulong)(a1), (ulong)(a2), 0)

#define CALL_ACTION_FUNC_3(func, t, a1, a2, a3)                    \
   (func)((t), (ulong)(a1), (ulong)(a2), (ulong)(a3))

static void
term_action_none(struct vterm *const t)
{
   /* do nothing */
}

DEFINE_TERM_ACTION_0(none)

static void
term_action_enable_cursor(struct vterm *const t, bool val)
{
   term_int_enable_cursor(t, val);
}

DEFINE_TERM_ACTION_1(enable_cursor, bool)

static void
term_action_scroll(struct vterm *const t,
                   u32 lines,
                   enum term_scroll_type st)
{
   ASSERT(st == term_scroll_up || st == term_scroll_down);

   if (st == term_scroll_up)
      term_int_scroll_up(t, lines);
   else
      term_int_scroll_down(t, lines);
}

DEFINE_TERM_ACTION_2(scroll, u32, enum term_scroll_type)

static void
term_action_non_buf_scroll(struct vterm *const t,
                           u16 n,
                           enum term_scroll_type st)
{
   ASSERT(st == term_scroll_up || st == term_scroll_down);

   if (st == term_scroll_up)
      term_internal_non_buf_scroll_up(t, n);
   else
      term_internal_non_buf_scroll_down(t, n);
}

DEFINE_TERM_ACTION_2(non_buf_scroll, u16, enum term_scroll_type)

static void
term_action_move_cur(struct vterm *const t, int row, int col)
{
   term_int_move_cur(t, row, col);
}

DEFINE_TERM_ACTION_2(move_cur, int, int)

static void
term_action_write(struct vterm *const t, const char *buf, u32 len, u8 color)
{
   const struct video_interface *const vi = t->vi;

   ts_scroll_to_bottom(t);
   vi->enable_cursor();

   for (u32 i = 0; i < len; i++) {

      if (UNLIKELY(t->filter == NULL)) {
         /* Early term use by printk(), before tty has been initialized */
         term_internal_write_char2(t, buf[i], color);
         continue;
      }

      /*
       * NOTE: We MUST store buf[i] in a local variable because the filter
       * function is absolutely allowed to modify its contents!!
       *
       * (Of course, buf is *not* required to point to writable memory.)
       */
      char c = buf[i];
      struct term_action a = { .type1 = a_none };
      enum term_fret r = t->filter((u8 *)&c, &color, &a, t->filter_ctx);

      if (LIKELY(r == TERM_FILTER_WRITE_C))
         term_internal_write_char2(t, c, color);

      term_execute_action(t, &a);
   }

   if (t->cursor_enabled)
      vi->move_cursor(t->r, t->c, get_curr_cell_fg_color(t));
}

DEFINE_TERM_ACTION_3(write, const char *, u32, u8)

/* Direct write without any filter nor move_cursor/flush */
static void
term_action_direct_write(struct vterm *const t, char *buf, u32 len, u8 color)
{
   for (u32 i = 0; i < len; i++)
      term_internal_write_char2(t, buf[i], color);
}

DEFINE_TERM_ACTION_3(direct_write, char *, u32, u8)

static void
term_action_set_col_offset(struct vterm *const t, u16 off)
{
   t->col_offset = off;
}

DEFINE_TERM_ACTION_1(set_col_offset, u16)

static void
term_action_move_cur_rel(struct vterm *const t, s16 dr, s16 dc)
{
   if (!t->buffer)
      return;

   t->r = (u16) CLAMP((int)t->r + dr, 0, t->rows - 1);
   t->c = (u16) CLAMP((int)t->c + dc, 0, t->cols - 1);

   if (t->cursor_enabled)
      t->vi->move_cursor(t->r, t->c, get_curr_cell_fg_color(t));
}

DEFINE_TERM_ACTION_2(move_cur_rel, s16, s16)

static void term_action_reset(struct vterm *const t)
{
   t->vi->enable_cursor();
   term_int_move_cur(t, 0, 0);
   t->scroll = t->max_scroll = 0;

   for (u16 i = 0; i < t->rows; i++)
      ts_clear_row(t, i, DEFAULT_COLOR16);

   if (t->tabs_buf)
      memset(t->tabs_buf, 0, t->cols * t->rows);
}

DEFINE_TERM_ACTION_0(reset)

static void
term_action_erase_in_display(struct vterm *const t, int mode)
{
   const u16 entry = make_vgaentry(' ', DEFAULT_COLOR16);

   switch (mode) {

      case 0:

         /* Clear the screen from the cursor position up to the end */

         for (u16 col = t->c; col < t->cols; col++) {
            buf_set_entry(t, t->r, col, entry);
            t->vi->set_char_at(t->r, col, entry);
         }

         for (u16 i = t->r + 1; i < t->rows; i++)
            ts_clear_row(t, i, DEFAULT_COLOR16);

         break;

      case 1:

         /* Clear the screen from the beginning up to cursor's position */

         for (u16 i = 0; i < t->r; i++)
            ts_clear_row(t, i, DEFAULT_COLOR16);

         for (u16 col = 0; col < t->c; col++) {
            buf_set_entry(t, t->r, col, entry);
            t->vi->set_char_at(t->r, col, entry);
         }

         break;

      case 2:

         /* Clear the whole screen */

         for (u16 i = 0; i < t->rows; i++)
            ts_clear_row(t, i, DEFAULT_COLOR16);

         break;

      case 3:
         /* Clear the whole screen and erase the scroll buffer */
         {
            u16 row = t->r;
            u16 col = t->c;
            term_action_reset(t);

            if (t->cursor_enabled)
               t->vi->move_cursor(row, col, DEFAULT_COLOR16);
         }
         break;

      default:
         return;
   }
}

DEFINE_TERM_ACTION_1(erase_in_display, int)

static void
term_action_erase_in_line(struct vterm *const t, int mode)
{
   const u16 entry = make_vgaentry(' ', DEFAULT_COLOR16);

   switch (mode) {

      case 0:
         for (u16 col = t->c; col < t->cols; col++) {
            buf_set_entry(t, t->r, col, entry);
            t->vi->set_char_at(t->r, col, entry);
         }
         break;

      case 1:
         for (u16 col = 0; col < t->c; col++) {
            buf_set_entry(t, t->r, col, entry);
            t->vi->set_char_at(t->r, col, entry);
         }
         break;

      case 2:
         ts_clear_row(t, t->r, vgaentry_get_color(entry));
         break;

      default:
         return;
   }
}

DEFINE_TERM_ACTION_1(erase_in_line, int)

static void
term_action_del(struct vterm *const t,
                enum term_del_type del_type,
                int m)
{
   switch (del_type) {

      case TERM_DEL_PREV_CHAR:
         term_internal_write_backspace(t, get_curr_cell_color(t));
         break;

      case TERM_DEL_PREV_WORD:
         term_internal_delete_last_word(t, get_curr_cell_color(t));
         break;

      case TERM_DEL_ERASE_IN_DISPLAY:
         term_action_erase_in_display(t, m);
         break;

      case TERM_DEL_ERASE_IN_LINE:
         term_action_erase_in_line(t, m);
         break;

      default:
         NOT_REACHED();
   }
}

DEFINE_TERM_ACTION_2(del, enum term_del_type, int)

static void
term_action_ins_blank_chars(struct vterm *const t, u16 n)
{
   const u16 row = t->r;
   u16 *const buf_row = get_buf_row(t, row);
   n = (u16)MIN(n, t->cols - t->c);

   memmove(&buf_row[t->c + n], &buf_row[t->c], 2 * (t->cols - t->c - n));

   for (u16 c = t->c; c < t->c + n; c++)
      buf_row[c] = make_vgaentry(' ', vgaentry_get_color(buf_row[c]));

   for (u16 c = t->c; c < t->cols; c++)
      t->vi->set_char_at(row, c, buf_row[c]);
}

DEFINE_TERM_ACTION_1(ins_blank_chars, u16)

static void
term_action_del_chars_in_line(struct vterm *const t, u16 n)
{
   const u16 row = t->r;
   u16 *const buf_row = get_buf_row(t, row);
   const u16 maxN = (u16)MIN(n, t->cols - t->c);
   const u16 cN = t->cols - t->c - maxN; /* copied count */

   memmove(&buf_row[t->c], &buf_row[t->c + maxN], 2 * cN);

   for (u16 c = t->c + cN; c < MIN(t->c + cN + n - maxN, t->cols); c++)
      buf_row[c] = make_vgaentry(' ', vgaentry_get_color(buf_row[c]));

   for (u16 c = t->c; c < t->cols; c++)
      t->vi->set_char_at(row, c, buf_row[c]);
}

DEFINE_TERM_ACTION_1(del_chars_in_line, u16)

static void
term_action_erase_chars_in_line(struct vterm *const t, u16 n)
{
   const u16 row = t->r;
   u16 *const buf_row = get_buf_row(t, row);

   for (u16 c = t->c; c < MIN(t->cols, t->c + n); c++)
      buf_row[c] = make_vgaentry(' ', vgaentry_get_color(buf_row[c]));

   for (u16 c = t->c; c < t->cols; c++)
      t->vi->set_char_at(row, c, buf_row[c]);
}

DEFINE_TERM_ACTION_1(erase_chars_in_line, u16)

static void
term_action_pause_output(struct vterm *const t)
{
   if (t->vi->disable_static_elems_refresh)
      t->vi->disable_static_elems_refresh();

   t->vi->disable_cursor();
   t->saved_vi = t->vi;
   t->vi = &no_output_vi;
}

DEFINE_TERM_ACTION_0(pause_output)

static void
term_action_restart_output(struct vterm *const t)
{
   t->vi = t->saved_vi;
   term_redraw(t);

   if (t->scroll == t->max_scroll)
      term_int_enable_cursor(t, t->cursor_enabled);

   if (!in_panic()) {
      if (t->vi->redraw_static_elements)
         t->vi->redraw_static_elements();

      if (t->vi->enable_static_elems_refresh)
         t->vi->enable_static_elems_refresh();
   }
}

DEFINE_TERM_ACTION_0(restart_output)

static void
term_action_use_alt_buffer(struct vterm *const t, bool use_alt_buffer)
{
   u16 *b = get_buf_row(t, 0);

   if (t->using_alt_buffer == use_alt_buffer)
      return;

   if (use_alt_buffer) {

      if (!t->screen_buf_copy) {

         if (term_allocate_alt_buffers(t) < 0)
            return; /* just do nothing: the main buffer will be used */
      }

      t->start_scroll_region = &t->alt_scroll_region_start;
      t->end_scroll_region = &t->alt_scroll_region_end;
      t->tabs_buf = t->alt_tabs_buf;
      t->saved_cur_row = t->r;
      t->saved_cur_col = t->c;
      memcpy(t->screen_buf_copy, b, sizeof(u16) * t->rows * t->cols);

   } else {

      ASSERT(t->screen_buf_copy != NULL);

      memcpy(b, t->screen_buf_copy, sizeof(u16) * t->rows * t->cols);
      t->r = t->saved_cur_row;
      t->c = t->saved_cur_col;
      t->tabs_buf = t->main_tabs_buf;
      t->start_scroll_region = &t->main_scroll_region_start;
      t->end_scroll_region = &t->main_scroll_region_end;
   }

   t->using_alt_buffer = use_alt_buffer;
   t->vi->disable_cursor();
   term_redraw(t);
   term_int_enable_cursor(t, t->cursor_enabled);
}

DEFINE_TERM_ACTION_1(use_alt_buffer, bool)

static void
term_action_ins_blank_lines(struct vterm *const t, u32 n)
{
   const u16 eR = *t->end_scroll_region + 1;

   if (!t->buffer || !n)
      return;

   if (t->r >= eR)
      return; /* we're outside the scrolling region: do nothing */

   t->c = 0;
   n = MIN(n, (u32)(eR - t->r));

   for (u32 row = eR - n; row > t->r; row--)
      buf_copy_row(t, row - 1 + n, row - 1);

   for (u16 row = t->r; row < t->r + n; row++)
      ts_buf_clear_row(t, row, DEFAULT_COLOR16);

   term_redraw_scroll_region(t);
}

DEFINE_TERM_ACTION_1(ins_blank_lines, u32)

static void
term_action_del_lines(struct vterm *const t, u32 n)
{
   const u16 eR = *t->end_scroll_region + 1;

   if (!t->buffer || !n)
      return;

   if (t->r >= eR)
      return; /* we're outside the scrolling region: do nothing */

   n = MIN(n, (u32)(eR - t->r));

   for (u32 row = t->r; row <= t->r + n; row++)
      buf_copy_row(t, row, row + n);

   for (u32 row = eR - n; row < eR; row++)
      ts_buf_clear_row(t, (u16)row, DEFAULT_COLOR16);

   term_redraw_scroll_region(t);
}

DEFINE_TERM_ACTION_1(del_lines, u32)

static void
term_action_set_scroll_region(struct vterm *const t, u16 start, u16 end)
{
   start = (u16) CLAMP(start, 0u, t->rows - 1u);
   end = (u16) CLAMP(end, 0u, t->rows - 1u);

   if (start >= end)
      return;

   *t->start_scroll_region = start;
   *t->end_scroll_region = end;
   term_int_move_cur(t, 0, 0);
}

DEFINE_TERM_ACTION_2(set_scroll_region, u16, u16)
