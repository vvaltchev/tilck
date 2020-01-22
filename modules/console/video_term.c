/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_modules.h>

#include <tilck/common/color_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/hal.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/safe_ringbuf.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/interrupts.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/errno.h>

#include "video_term_int.h"

struct term {

   bool initialized;
   bool cursor_enabled;
   bool using_alt_buffer;

   u16 tabsize;               /* term's current tab size */
   u16 rows;                  /* term's rows count */
   u16 cols;                  /* term's columns count */

   u16 r;                     /* current row */
   u16 c;                     /* current col */
   u16 col_offset;

   const struct video_interface *vi;
   const struct video_interface *saved_vi;

   u16 *buffer;               /* the whole screen buffer */
   u16 *screen_buf_copy;      /* when != NULL, contains one screenshot */
   u32 scroll;                /* != max_scroll only while scrolling */
   u32 max_scroll;            /* buffer rows used - rows. Its value is 0 until
                                 the screen scrolls for the first time */
   u32 total_buffer_rows;     /* >= term rows */
   u32 extra_buffer_rows;     /* => total_buffer_rows - rows. Always >= 0 */

   u16 saved_cur_row;         /* keeps primary buffer's cursor's row */
   u16 saved_cur_col;         /* keeps primary buffer's cursor's col */

   bool *tabs_buf;
   bool *main_tabs_buf;
   bool *alt_tabs_buf;

   struct safe_ringbuf ringb;
   struct term_action actions_buf[32];

   term_filter filter;
   void *filter_ctx;
};

static struct term first_instance;
static u16 failsafe_buffer[80 * 25];

/* ------------ No-output video-interface ------------------ */

static void no_vi_set_char_at(u16 row, u16 col, u16 entry) { }
static void no_vi_set_row(u16 row, u16 *data, bool flush) { }
static void no_vi_clear_row(u16 row_num, u8 color) { }
static void no_vi_move_cursor(u16 row, u16 col, int color) { }
static void no_vi_enable_cursor(void) { }
static void no_vi_disable_cursor(void) { }
static void no_vi_scroll_one_line_up(void) { }
static void no_vi_flush_buffers(void) { }
static void no_vi_redraw_static_elements(void) { }
static void no_vi_disable_static_elems_refresh(void) { }
static void no_vi_enable_static_elems_refresh(void) { }

static const struct video_interface no_output_vi =
{
   no_vi_set_char_at,
   no_vi_set_row,
   no_vi_clear_row,
   no_vi_move_cursor,
   no_vi_enable_cursor,
   no_vi_disable_cursor,
   no_vi_scroll_one_line_up,
   no_vi_flush_buffers,
   no_vi_redraw_static_elements,
   no_vi_disable_static_elems_refresh,
   no_vi_enable_static_elems_refresh
};

/* --------------------------------------------------------- */

static ALWAYS_INLINE void
buffer_set_entry(struct term *t, u16 row, u16 col, u16 e)
{
   t->buffer[(row + t->scroll) % t->total_buffer_rows * t->cols + col] = e;
}

static ALWAYS_INLINE
u16 buffer_get_entry(struct term *t, u16 row, u16 col)
{
   return t->buffer[(row + t->scroll) % t->total_buffer_rows * t->cols + col];
}

static ALWAYS_INLINE bool ts_is_at_bottom(struct term *t)
{
   return t->scroll == t->max_scroll;
}

static ALWAYS_INLINE u8 get_curr_cell_color(struct term *t)
{
   if (!t->buffer)
      return 0;

   return vgaentry_get_color(buffer_get_entry(t, t->r, t->c));
}

static void term_action_enable_cursor(struct term *t, u16 val, ...)
{
   if (val == 0) {

      t->vi->disable_cursor();
      t->cursor_enabled = false;

   } else {

      ASSERT(val == 1);
      t->vi->enable_cursor();
      t->vi->move_cursor(t->r, t->c, get_curr_cell_color(t));
      t->cursor_enabled = true;
   }
}

static void term_redraw(struct term *t)
{
   if (!t->buffer)
      return;

   fpu_context_begin();

   for (u16 row = 0; row < t->rows; row++) {
      u32 buffer_row = (t->scroll + row) % t->total_buffer_rows;
      t->vi->set_row(row, &t->buffer[t->cols * buffer_row], true);
   }

   fpu_context_end();
}

static void ts_set_scroll(struct term *t, u32 requested_scroll)
{
   /*
    * 1. scroll cannot be > max_scroll
    * 2. scroll cannot be < max_scroll - extra_buffer_rows, where
    *    extra_buffer_rows = total_buffer_rows - VIDEO_ROWS.
    *    In other words, if for example total_buffer_rows is 26, and max_scroll
    *    is 1000, scroll cannot be less than 1000 + 25 - 26 = 999, which means
    *    exactly 1 scroll row (extra_buffer_rows == 1).
    */

   const u32 min_scroll =
      t->max_scroll > t->extra_buffer_rows
         ? t->max_scroll - t->extra_buffer_rows
         : 0;

   requested_scroll = CLAMP(requested_scroll, min_scroll, t->max_scroll);

   if (requested_scroll == t->scroll)
      return; /* nothing to do */

   t->scroll = requested_scroll;
   term_redraw(t);
}

static ALWAYS_INLINE void ts_scroll_up(struct term *t, u32 lines)
{
   if (lines > t->scroll)
      ts_set_scroll(t, 0);
   else
      ts_set_scroll(t, t->scroll - lines);
}

static ALWAYS_INLINE void ts_scroll_down(struct term *t, u32 lines)
{
   ts_set_scroll(t, t->scroll + lines);
}

static ALWAYS_INLINE void ts_scroll_to_bottom(struct term *t)
{
   if (t->scroll != t->max_scroll) {
      ts_set_scroll(t, t->max_scroll);
   }
}

static void ts_buf_clear_row(struct term *t, u16 row, u8 color)
{
   if (!t->buffer)
      return;

   u16 *rowb = t->buffer + t->cols * ((row + t->scroll) % t->total_buffer_rows);
   memset16(rowb, make_vgaentry(' ', color), t->cols);
}

static void ts_clear_row(struct term *t, u16 row, u8 color)
{
   ts_buf_clear_row(t, row, color);
   t->vi->clear_row(row, color);
}

/* ---------------- term actions --------------------- */

static void term_execute_action(struct term *t, struct term_action *a);

static void term_int_scroll_up(struct term *t, u32 lines)
{
   ts_scroll_up(t, lines);

   if (t->cursor_enabled) {

      if (!ts_is_at_bottom(t)) {

         t->vi->disable_cursor();

      } else {

         t->vi->enable_cursor();
         t->vi->move_cursor(t->r, t->c, get_curr_cell_color(t));
      }
   }

   if (t->vi->flush_buffers)
      t->vi->flush_buffers();
}

static void term_int_scroll_down(struct term *t, u32 lines)
{
   ts_scroll_down(t, lines);

   if (t->cursor_enabled) {
      if (ts_is_at_bottom(t)) {
         t->vi->enable_cursor();
         t->vi->move_cursor(t->r, t->c, get_curr_cell_color(t));
      }
   }

   if (t->vi->flush_buffers)
      t->vi->flush_buffers();
}

static void
term_action_scroll(struct term *t, u32 lines, enum term_scroll_type st, ...)
{
   if (st == term_scroll_up) {
      term_int_scroll_up(t, lines);
   } else {
      ASSERT(st == term_scroll_down);
      term_int_scroll_down(t, lines);
   }
}

static void term_internal_incr_row(struct term *t, u8 color)
{
   t->col_offset = 0;

   if (t->r < t->rows - 1) {
      ++t->r;
      return;
   }

   t->max_scroll++;

   if (t->vi->scroll_one_line_up) {
      t->scroll++;
      t->vi->scroll_one_line_up();
   } else {
      ts_set_scroll(t, t->max_scroll);
   }

   ts_clear_row(t, t->rows - 1, color);
}

static void term_internal_write_printable_char(struct term *t, u8 c, u8 color)
{
   const u16 entry = make_vgaentry(c, color);
   buffer_set_entry(t, t->r, t->c, entry);
   t->vi->set_char_at(t->r, t->c, entry);
   t->c++;
}

static void term_internal_write_tab(struct term *t, u8 color)
{
   int rem = t->cols - t->c - 1;

   if (!t->tabs_buf) {

      if (rem)
         term_internal_write_printable_char(t, ' ', color);

      return;
   }

   u32 tab_col = (u32) MIN(round_up_at(t->c+1, t->tabsize), (u32)t->cols-1) - 1;
   t->tabs_buf[t->r * t->cols + tab_col] = 1;
   t->c = (u16)(tab_col + 1);
}

static void term_internal_write_backspace(struct term *t, u8 color)
{
   if (!t->c || t->c <= t->col_offset)
      return;

   const u16 space_entry = make_vgaentry(' ', color);
   t->c--;

   if (!t->tabs_buf || !t->tabs_buf[t->r * t->cols + t->c]) {
      buffer_set_entry(t, t->r, t->c, space_entry);
      t->vi->set_char_at(t->r, t->c, space_entry);
      return;
   }

   /* we hit the end of a tab */
   t->tabs_buf[t->r * t->cols + t->c] = 0;

   for (int i = t->tabsize - 1; i >= 0; i--) {

      if (!t->c || t->c == t->col_offset)
         break;

      if (t->tabs_buf[t->r * t->cols + t->c - 1])
         break; /* we hit the previous tab */

      if (vgaentry_get_char(buffer_get_entry(t, t->r, t->c - 1)) != ' ')
         break;

      t->c--;
   }
}

static void term_internal_delete_last_word(struct term *t, u8 color)
{
   u8 c;

   while (t->c > 0) {

      c = vgaentry_get_char(buffer_get_entry(t, t->r, t->c - 1));

      if (c != ' ')
         break;

      term_internal_write_backspace(t, color);
   }

   while (t->c > 0) {

      c = vgaentry_get_char(buffer_get_entry(t, t->r, t->c - 1));

      if (c == ' ')
         break;

      term_internal_write_backspace(t, color);
   }
}

static void term_internal_write_char2(struct term *t, char c, u8 color)
{
   switch (c) {

      case '\n':
         term_internal_incr_row(t, color);
         break;

      case '\r':
         t->c = 0;
         break;

      case '\t':
         term_internal_write_tab(t, color);
         break;

      default:

         if (t->c == t->cols) {
            t->c = 0;
            term_internal_incr_row(t, color);
         }

         term_internal_write_printable_char(t, (u8)c, color);
         break;
   }
}

static void term_action_write(struct term *t, char *buf, u32 len, u8 color)
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

      if (UNLIKELY(a.type1 != a_none))
         term_execute_action(t, &a);
   }

   if (t->cursor_enabled)
      vi->move_cursor(t->r, t->c, get_curr_cell_color(t));

   if (vi->flush_buffers)
      vi->flush_buffers();
}

/* Direct write without any filter nor move_cursor/flush */
static void
term_action_dwrite_no_filter(struct term *t, char *buf, u32 len, u8 color)
{
   for (u32 i = 0; i < len; i++)
      term_internal_write_char2(t, buf[i], color);
}

static void term_action_set_col_offset(struct term *t, u16 off, ...)
{
   t->col_offset = off;
}

static void term_action_move_ch_and_cur(struct term *t, int row, int col, ...)
{
   if (!t->buffer)
      return;

   t->r = (u16) CLAMP(row, 0, t->rows - 1);
   t->c = (u16) CLAMP(col, 0, t->cols - 1);

   if (t->cursor_enabled)
      t->vi->move_cursor(t->r, t->c, get_curr_cell_color(t));

   if (t->vi->flush_buffers)
      t->vi->flush_buffers();
}

static void term_action_move_ch_and_cur_rel(struct term *t, s8 dr, s8 dc, ...)
{
   if (!t->buffer)
      return;

   t->r = (u16) CLAMP((int)t->r + dr, 0, t->rows - 1);
   t->c = (u16) CLAMP((int)t->c + dc, 0, t->cols - 1);

   if (t->cursor_enabled)
      t->vi->move_cursor(t->r, t->c, get_curr_cell_color(t));

   if (t->vi->flush_buffers)
      t->vi->flush_buffers();
}

static void term_action_reset(struct term *t, ...)
{
   t->vi->enable_cursor();
   term_action_move_ch_and_cur(t, 0, 0);
   t->scroll = t->max_scroll = 0;

   for (u16 i = 0; i < t->rows; i++)
      ts_clear_row(t, i, DEFAULT_COLOR16);

   if (t->tabs_buf)
      memset(t->tabs_buf, 0, t->cols * t->rows);
}

static void term_action_erase_in_display(struct term *t, int mode, ...)
{
   const u16 entry = make_vgaentry(' ', DEFAULT_COLOR16);

   switch (mode) {

      case 0:

         /* Clear the screen from the cursor position up to the end */

         for (u16 col = t->c; col < t->cols; col++) {
            buffer_set_entry(t, t->r, col, entry);
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
            buffer_set_entry(t, t->r, col, entry);
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

   if (t->vi->flush_buffers)
      t->vi->flush_buffers();
}

static void term_action_erase_in_line(struct term *t, int mode, ...)
{
   const u16 entry = make_vgaentry(' ', DEFAULT_COLOR16);

   switch (mode) {

      case 0:
         for (u16 col = t->c; col < t->cols; col++) {
            buffer_set_entry(t, t->r, col, entry);
            t->vi->set_char_at(t->r, col, entry);
         }
         break;

      case 1:
         for (u16 col = 0; col < t->c; col++) {
            buffer_set_entry(t, t->r, col, entry);
            t->vi->set_char_at(t->r, col, entry);
         }
         break;

      case 2:
         ts_clear_row(t, t->r, vgaentry_get_color(entry));
         break;

      default:
         return;
   }

   if (t->vi->flush_buffers)
      t->vi->flush_buffers();
}

static void
term_action_del(struct term *t, enum term_del_type del_type, int m, ...)
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

static void term_action_non_buf_scroll_up(struct term *t, u16 n, ...)
{
   if (!t->buffer)
      return;

   ASSERT(n >= 1);
   n = (u16)MIN(n, t->rows);

   for (u16 row = 0; row < (u16)(t->rows - n); row++) {
      u32 s = (t->scroll + row + n) % t->total_buffer_rows;
      u32 d = (t->scroll + row) % t->total_buffer_rows;
      memcpy(&t->buffer[t->cols * d], &t->buffer[t->cols * s], t->cols * 2);
   }

   for (u16 row = t->rows - n; row < t->rows; row++)
      ts_buf_clear_row(t, row, DEFAULT_COLOR16);

   term_redraw(t);
}

static void term_action_non_buf_scroll_down(struct term *t, u16 n, ...)
{
   if (!t->buffer)
      return;

   ASSERT(n >= 1);
   n = (u16)MIN(n, t->rows);

   for (int row = (int)t->rows - n - 1; row >= 0; row--) {
      u32 s = (t->scroll + (u32)row) % t->total_buffer_rows;
      u32 d = (t->scroll + (u32)row + n) % t->total_buffer_rows;
      memcpy(&t->buffer[t->cols * d], &t->buffer[t->cols * s], t->cols * 2);
   }

   for (u16 row = 0; row < n; row++)
      ts_buf_clear_row(t, row, DEFAULT_COLOR16);

   term_redraw(t);
}

static void term_action_non_buf_scroll(struct term *t, u16 n, u16 dir, ...)
{
   if (dir == term_scroll_up) {

      term_action_non_buf_scroll_up(t, n);

   } else {

      ASSERT(dir == term_scroll_down);
      term_action_non_buf_scroll_down(t, n);
   }
}

static void term_action_pause_video_output(struct term *t, ...)
{
   if (t->vi->disable_static_elems_refresh)
      t->vi->disable_static_elems_refresh();

   t->vi->disable_cursor();
   t->saved_vi = t->vi;
   t->vi = &no_output_vi;
}

static void term_action_restart_video_output(struct term *t, ...)
{
   t->vi = t->saved_vi;

   term_redraw(t);
   term_action_enable_cursor(t, t->cursor_enabled);

   if (t->vi->redraw_static_elements)
      t->vi->redraw_static_elements();

   if (t->vi->enable_static_elems_refresh)
      t->vi->enable_static_elems_refresh();
}

static int
term_allocate_alt_buffers(struct term *t)
{
   t->screen_buf_copy = kmalloc(sizeof(u16) * t->rows * t->cols);

   if (!t->screen_buf_copy)
      return -ENOMEM;

   if (!t->alt_tabs_buf) {

      t->alt_tabs_buf = kzmalloc(t->rows * t->cols);

      if (!t->alt_tabs_buf) {
         kfree2(t->screen_buf_copy, sizeof(u16) * t->rows * t->cols);
         t->screen_buf_copy = NULL;
         return -ENOMEM;
      }
   }

   return 0;
}

static void
term_action_use_alt_buffer(struct term *t, bool use_alt_buffer, ...)
{
   u16 *b = &t->buffer[t->scroll % t->total_buffer_rows * t->cols];

   if (t->using_alt_buffer == use_alt_buffer)
      return;

   if (use_alt_buffer) {

      if (!t->screen_buf_copy) {

         if (term_allocate_alt_buffers(t) < 0)
            return; /* just do nothing: the main buffer will be used */
      }

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
   }

   t->using_alt_buffer = use_alt_buffer;
   t->vi->disable_cursor();
   term_redraw(t);
   term_action_enable_cursor(t, t->cursor_enabled);
}

#include "term_action_wrappers.c.h"

#ifdef DEBUG

static void
debug_term_dump_font_table(struct term *t)
{
   static const u8 hex_digits[] = "0123456789abcdef";
   const u8 color = DEFAULT_COLOR16;

   term_internal_incr_row(t, color);
   t->c = 0;

   for (u32 i = 0; i < 6; i++)
      term_internal_write_printable_char(t, ' ', color);

   for (u32 i = 0; i < 16; i++) {
      term_internal_write_printable_char(t, hex_digits[i], color);
      term_internal_write_printable_char(t, ' ', color);
   }

   term_internal_incr_row(t, color);
   term_internal_incr_row(t, color);
   t->c = 0;

   for (u8 i = 0; i < 16; i++) {

      term_internal_write_printable_char(t, '0', color);
      term_internal_write_printable_char(t, 'x', color);
      term_internal_write_printable_char(t, hex_digits[i], color);

      for (u8 j = 0; j < 3; j++)
         term_internal_write_printable_char(t, ' ', color);

      for (u8 j = 0; j < 16; j++) {

         u8 c = i * 16 + j;
         term_internal_write_printable_char(t, c, color);
         term_internal_write_printable_char(t, ' ', color);
      }

      term_internal_incr_row(t, color);
      t->c = 0;
   }

   term_internal_incr_row(t, color);
   t->c = 0;
}

#endif

static struct term *
alloc_term_struct(void)
{
   return kzmalloc(sizeof(struct term));
}

static void
free_term_struct(struct term *t)
{
   ASSERT(t != &first_instance);
   kfree2(t, sizeof(struct term));
}

static void
dispose_term(struct term *t)
{
   ASSERT(t != &first_instance);

   if (t->buffer) {
      kfree2(t->buffer, sizeof(u16) * t->total_buffer_rows * t->cols);
      t->buffer = NULL;
   }

   if (t->main_tabs_buf) {
      kfree2(t->main_tabs_buf, t->cols * t->rows);
      t->main_tabs_buf = NULL;
   }

   if (t->alt_tabs_buf) {
      kfree2(t->alt_tabs_buf, t->cols * t->rows);
      t->alt_tabs_buf = NULL;
   }

   if (t->screen_buf_copy) {
      kfree2(t->screen_buf_copy, sizeof(u16) * t->rows * t->cols);
      t->screen_buf_copy = NULL;
   }
}

static void
vterm_get_params(struct term *t, struct term_params *out)
{
   *out = (struct term_params) {
      .rows = t->rows,
      .cols = t->cols,
      .type = term_type_video,
      .vi = t->vi,
   };
}

/*
 * Calculate an optimal number of extra buffer rows to use for a term of size
 * `rows` x `cols`, in order to minimize the memory waste (happening when the
 * buffer size is not a power of 2).
 */
static u32 term_calc_extra_buf_rows(u16 rows, u16 cols)
{
   u32 buf_size = 0;

   if (cols <= 80)
      buf_size = 32 * KB;
   else if (cols <= 100)
      buf_size = 64 * KB;
   else if (cols <= 128)
      buf_size = 128 * KB;
   else
      buf_size = 256 * KB;

   if (TERM_BIG_SCROLL_BUF)
      buf_size *= 4;

   return (buf_size / 2) / cols - rows;
}

static int
init_vterm(struct term *t,
           const struct video_interface *intf,
           u16 rows,
           u16 cols,
           int rows_buf)
{
   ASSERT(t != &first_instance || !are_interrupts_enabled());

   t->tabsize = 8;
   t->cols = cols;
   t->rows = rows;
   t->saved_vi = intf;
   t->vi = (t == &first_instance) ? intf : &no_output_vi;

   safe_ringbuf_init(&t->ringb,
                     ARRAY_SIZE(t->actions_buf),
                     sizeof(struct term_action),
                     t->actions_buf);

   if (!in_panic()) {

      t->extra_buffer_rows =
         rows_buf >= 0
            ? (u32)rows_buf
            : term_calc_extra_buf_rows(rows, cols);

      t->total_buffer_rows = t->rows + t->extra_buffer_rows;

      if (is_kmalloc_initialized())
         t->buffer = kmalloc(2 * t->total_buffer_rows * t->cols);
   }

   if (t->buffer) {

      t->main_tabs_buf = kzmalloc(t->cols * t->rows);

      if (t->main_tabs_buf) {

         t->tabs_buf = t->main_tabs_buf;

      } else {

         if (t != &first_instance) {
            kfree2(t->buffer, 2 * t->total_buffer_rows * t->cols);
            return -ENOMEM;
         }

         printk("WARNING: unable to allocate main_tabs_buf\n");
      }

   } else {

      /* We're in panic or we were unable to allocate the buffer */

      if (t != &first_instance)
         return -ENOMEM;

      t->cols = (u16) MIN((u16)80, t->cols);
      t->rows = (u16) MIN((u16)25, t->rows);

      t->extra_buffer_rows = 0;
      t->total_buffer_rows = t->rows;
      t->buffer = failsafe_buffer;

      if (!in_panic())
         printk("ERROR: unable to allocate the term buffer.\n");
   }

   t->cursor_enabled = true;
   t->vi->enable_cursor();
   term_action_move_ch_and_cur(t, 0, 0);

   for (u16 i = 0; i < t->rows; i++)
      ts_clear_row(t, i, DEFAULT_COLOR16);

   t->initialized = true;
   return 0;
}

static struct term *vterm_get_first_inst(void)
{
   return &first_instance;
}

static enum term_type vterm_get_type(void)
{
   return term_type_video;
}

static const struct term_interface intf = {

   .get_type = vterm_get_type,
   .is_initialized = vterm_is_initialized,
   .get_params = vterm_get_params,

   .write = vterm_write,
   .scroll_up = vterm_scroll_up,
   .scroll_down = vterm_scroll_down,
   .set_col_offset = vterm_set_col_offset,
   .pause_video_output = vterm_pause_video_output,
   .restart_video_output = vterm_restart_video_output,
   .set_filter = vterm_set_filter,

   .get_first_term = vterm_get_first_inst,
   .video_term_init = init_vterm,
   .serial_term_init = NULL,
   .alloc = alloc_term_struct,
   .free = free_term_struct,
   .dispose = dispose_term,

#ifdef DEBUG
   .debug_dump_font_table = debug_term_dump_font_table,
#endif
};

__attribute__((constructor))
static void register_term(void)
{
   register_term_intf(&intf);
}
