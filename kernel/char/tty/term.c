/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/color_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/hal.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/serial.h>
#include <tilck/kernel/ringbuf.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/interrupts.h>
#include <tilck/kernel/cmdline.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/errno.h>

#define _TERM_C_

#include "term_int.h"

struct term {

   bool initialized;
   u16 tabsize;
   u16 cols;
   u16 rows;

   u16 r; /* current row */
   u16 c; /* current col */
   u16 term_col_offset;

   const video_interface *vi;
   const video_interface *saved_vi;

   u16 *buffer;
   u32 scroll;
   u32 max_scroll;

   u32 total_buffer_rows;
   u32 extra_buffer_rows;
   bool *term_tabs_buf;

   ringbuf ringbuf;
   term_action actions_buf[32];

   term_filter filter;
   void *filter_ctx;
};

static term first_instance;
static u16 failsafe_buffer[80 * 25];

term *__curr_term = &first_instance;

/* ------------ No-output video-interface ------------------ */

void no_vi_set_char_at(u16 row, u16 col, u16 entry) { }
void no_vi_set_row(u16 row, u16 *data, bool flush) { }
void no_vi_clear_row(u16 row_num, u8 color) { }
void no_vi_move_cursor(u16 row, u16 col, int color) { }
void no_vi_enable_cursor(void) { }
void no_vi_disable_cursor(void) { }
void no_vi_scroll_one_line_up(void) { }
void no_vi_flush_buffers(void) { }
void no_vi_redraw_static_elements(void) { }
void no_vi_disable_static_elems_refresh(void) { }
void no_vi_enable_static_elems_refresh(void) { }

static const video_interface no_output_vi =
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

static ALWAYS_INLINE void buffer_set_entry(term *t, u16 row, u16 col, u16 e)
{
   t->buffer[(row + t->scroll) % t->total_buffer_rows * t->cols + col] = e;
}

static ALWAYS_INLINE u16 buffer_get_entry(term *t, u16 row, u16 col)
{
   return t->buffer[(row + t->scroll) % t->total_buffer_rows * t->cols + col];
}

static ALWAYS_INLINE bool ts_is_at_bottom(term *t)
{
   return t->scroll == t->max_scroll;
}

static ALWAYS_INLINE u8 get_curr_cell_color(term *t)
{
   return vgaentry_get_color(buffer_get_entry(t, t->r, t->c));
}

static void term_redraw(term *t)
{
   fpu_context_begin();

   for (u16 row = 0; row < t->rows; row++) {
      u32 buffer_row = (t->scroll + row) % t->total_buffer_rows;
      t->vi->set_row(row, &t->buffer[t->cols * buffer_row], true);
   }

   fpu_context_end();
}

static void ts_set_scroll(term *t, u32 requested_scroll)
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

   requested_scroll = BOUND(requested_scroll, min_scroll, t->max_scroll);

   if (requested_scroll == t->scroll)
      return; /* nothing to do */

   t->scroll = requested_scroll;
   term_redraw(t);
}

static ALWAYS_INLINE void ts_scroll_up(term *t, u32 lines)
{
   if (lines > t->scroll)
      ts_set_scroll(t, 0);
   else
      ts_set_scroll(t, t->scroll - lines);
}

static ALWAYS_INLINE void ts_scroll_down(term *t, u32 lines)
{
   ts_set_scroll(t, t->scroll + lines);
}

static ALWAYS_INLINE void ts_scroll_to_bottom(term *t)
{
   if (t->scroll != t->max_scroll) {
      ts_set_scroll(t, t->max_scroll);
   }
}

static void ts_buf_clear_row(term *t, u16 row, u8 color)
{
   u16 *rowb = t->buffer + t->cols * ((row + t->scroll) % t->total_buffer_rows);
   memset16(rowb, make_vgaentry(' ', color), t->cols);
}

static void ts_clear_row(term *t, u16 row, u8 color)
{
   ts_buf_clear_row(t, row, color);
   t->vi->clear_row(row, color);
}

/* ---------------- term actions --------------------- */

static void term_execute_action(term *t, term_action *a);

static void term_int_scroll_up(term *t, u32 lines)
{
   ts_scroll_up(t, lines);

   if (!ts_is_at_bottom(t)) {
      t->vi->disable_cursor();
   } else {
      t->vi->enable_cursor();
      t->vi->move_cursor(t->r, t->c, get_curr_cell_color(t));
   }

   if (t->vi->flush_buffers)
      t->vi->flush_buffers();
}

static void term_int_scroll_down(term *t, u32 lines)
{
   ts_scroll_down(t, lines);

   if (ts_is_at_bottom(t)) {
      t->vi->enable_cursor();
      t->vi->move_cursor(t->r, t->c, get_curr_cell_color(t));
   }

   if (t->vi->flush_buffers)
      t->vi->flush_buffers();
}

static void term_action_scroll(term *t, u32 lines, bool down, ...)
{
   if (!down)
      term_int_scroll_up(t, lines);
   else
      term_int_scroll_down(t, lines);
}

static void term_internal_incr_row(term *t, u8 color)
{
   t->term_col_offset = 0;

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

static void term_internal_write_printable_char(term *t, u8 c, u8 color)
{
   u16 entry = make_vgaentry(c, color);
   buffer_set_entry(t, t->r, t->c, entry);
   t->vi->set_char_at(t->r, t->c, entry);
   t->c++;
}

static void term_internal_write_tab(term *t, u8 color)
{
   int rem = t->cols - t->c - 1;

   if (!t->term_tabs_buf) {

      if (rem)
         term_internal_write_printable_char(t, ' ', color);

      return;
   }

   u32 tab_col = (u32) MIN(round_up_at(t->c+1, t->tabsize), (u32)t->cols-1) - 1;
   t->term_tabs_buf[t->r * t->cols + tab_col] = 1;
   t->c = (u16)(tab_col + 1);
}

static void term_internal_write_backspace(term *t, u8 color)
{
   if (!t->c || t->c <= t->term_col_offset)
      return;

   const u16 space_entry = make_vgaentry(' ', color);
   t->c--;

   if (!t->term_tabs_buf || !t->term_tabs_buf[t->r * t->cols + t->c]) {
      buffer_set_entry(t, t->r, t->c, space_entry);
      t->vi->set_char_at(t->r, t->c, space_entry);
      return;
   }

   /* we hit the end of a tab */
   t->term_tabs_buf[t->r * t->cols + t->c] = 0;

   for (int i = t->tabsize - 1; i >= 0; i--) {

      if (!t->c || t->c == t->term_col_offset)
         break;

      if (t->term_tabs_buf[t->r * t->cols + t->c - 1])
         break; /* we hit the previous tab */

      if (vgaentry_get_char(buffer_get_entry(t, t->r, t->c - 1)) != ' ')
         break;

      t->c--;
   }
}

static void term_internal_delete_last_word(term *t, u8 color)
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

static void term_action_del(term *t, enum term_del_type del_type, ...)
{
   switch (del_type) {

      case TERM_DEL_PREV_CHAR:
         term_internal_write_backspace(t, get_curr_cell_color(t));
         break;

      case TERM_DEL_PREV_WORD:
         term_internal_delete_last_word(t, get_curr_cell_color(t));
         break;

      default:
         NOT_REACHED();
   }
}

static void term_serial_con_write(char c)
{
   serial_write(COM1, c);
}

static void term_internal_write_char2(term *t, char c, u8 color)
{
   if (kopt_serial_console) {
      serial_write(COM1, c);
      return;
   }

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

static void term_action_write(term *t, char *buf, u32 len, u8 color)
{
   const video_interface *const vi = t->vi;

   ts_scroll_to_bottom(t);
   vi->enable_cursor();

   for (u32 i = 0; i < len; i++) {

      if (UNLIKELY(t->filter == NULL)) {
         /* Early term use by printk(), before tty has been initialized */
         term_internal_write_char2(t, buf[i], color);
         continue;
      }

      term_action a = { .type1 = a_none };
      enum term_fret r = t->filter((u8 *)&buf[i], &color, &a, t->filter_ctx);

      if (LIKELY(r == TERM_FILTER_WRITE_C))
         term_internal_write_char2(t, buf[i], color);

      if (UNLIKELY(a.type1 != a_none))
         term_execute_action(t, &a);
   }

   vi->move_cursor(t->r, t->c, get_curr_cell_color(t));

   if (vi->flush_buffers)
      vi->flush_buffers();
}

/*
 * Direct write w/o any filter nor scroll/move_cursor/flush.
 */
static void
term_action_dwrite_no_filter(term *t, char *buf, u32 len, u8 color)
{
   for (u32 i = 0; i < len; i++) {
      term_internal_write_char2(t, buf[i], color);
   }
}

static void term_action_set_col_offset(term *t, u16 off, ...)
{
   t->term_col_offset = off;
}

static void term_action_move_ch_and_cur(term *t, int row, int col, ...)
{
   t->r = (u16) BOUND(row, 0, t->rows - 1);
   t->c = (u16) BOUND(col, 0, t->cols - 1);
   t->vi->move_cursor(t->r, t->c, get_curr_cell_color(t));

   if (t->vi->flush_buffers)
      t->vi->flush_buffers();
}

static void term_action_move_ch_and_cur_rel(term *t, s8 dr, s8 dc, ...)
{
   t->r = (u16) BOUND((int)t->r + dr, 0, t->rows - 1);
   t->c = (u16) BOUND((int)t->c + dc, 0, t->cols - 1);
   t->vi->move_cursor(t->r, t->c, get_curr_cell_color(t));

   if (t->vi->flush_buffers)
      t->vi->flush_buffers();
}

static void term_action_reset(term *t, ...)
{
   t->vi->enable_cursor();
   term_action_move_ch_and_cur(t, 0, 0);
   t->scroll = t->max_scroll = 0;

   for (u16 i = 0; i < t->rows; i++)
      ts_clear_row(t, i, make_color(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));

   if (t->term_tabs_buf)
      memset(t->term_tabs_buf, 0, t->cols * t->rows);
}

static void term_action_erase_in_display(term *t, int mode, ...)
{
   static const u16 entry =
      make_vgaentry(' ', make_color(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));

   switch (mode) {

      case 0:

         /* Clear the screen from the cursor position up to the end */

         for (u16 col = t->c; col < t->cols; col++) {
            buffer_set_entry(t, t->r, col, entry);
            t->vi->set_char_at(t->r, col, entry);
         }

         for (u16 i = t->r + 1; i < t->rows; i++)
            ts_clear_row(t, i, make_color(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));

         break;

      case 1:

         /* Clear the screen from the beginning up to cursor's position */

         for (u16 i = 0; i < t->r; i++)
            ts_clear_row(t, i, make_color(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));

         for (u16 col = 0; col < t->c; col++) {
            buffer_set_entry(t, t->r, col, entry);
            t->vi->set_char_at(t->r, col, entry);
         }

         break;

      case 2:

         /* Clear the whole screen */

         for (u16 i = 0; i < t->rows; i++)
            ts_clear_row(t, i, make_color(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));

         break;

      case 3:
         /* Clear the whole screen and erase the t->scroll t->buffer */
         {
            u16 row = t->r;
            u16 col = t->c;
            term_action_reset(t);
            t->vi->move_cursor(row, col, make_color(DEFAULT_FG_COLOR,
                                                    DEFAULT_BG_COLOR));
         }
         break;

      default:
         return;
   }

   if (t->vi->flush_buffers)
      t->vi->flush_buffers();
}

static void term_action_erase_in_line(term *t, int mode, ...)
{
   static const u16 entry =
      make_vgaentry(' ', make_color(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));

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

static void term_action_non_buf_scroll_up(term *t, u16 n, ...)
{
   ASSERT(n >= 1);
   n = (u16)MIN(n, t->rows);

   for (u16 row = 0; row < (u16)(t->rows - n); row++) {
      u32 s = (t->scroll + row + n) % t->total_buffer_rows;
      u32 d = (t->scroll + row) % t->total_buffer_rows;
      memcpy(&t->buffer[t->cols * d], &t->buffer[t->cols * s], t->cols * 2);
   }

   for (u16 row = t->rows - n; row < t->rows; row++)
      ts_buf_clear_row(t, row, make_color(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));

   term_redraw(t);
}

static void term_action_non_buf_scroll_down(term *t, u16 n, ...)
{
   ASSERT(n >= 1);
   n = (u16)MIN(n, t->rows);

   for (int row = (int)t->rows - n - 1; row >= 0; row--) {
      u32 s = (t->scroll + (u32)row) % t->total_buffer_rows;
      u32 d = (t->scroll + (u32)row + n) % t->total_buffer_rows;
      memcpy(&t->buffer[t->cols * d], &t->buffer[t->cols * s], t->cols * 2);
   }

   for (u16 row = 0; row < n; row++)
      ts_buf_clear_row(t, row, make_color(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));

   term_redraw(t);
}

static void term_action_pause_video_output(term *t, ...)
{
   if (t->vi->disable_static_elems_refresh)
      t->vi->disable_static_elems_refresh();

   t->vi->disable_cursor();
   t->saved_vi = t->vi;
   t->vi = &no_output_vi;
}

static void term_action_restart_video_output(term *t, ...)
{
   t->vi = t->saved_vi;

   term_redraw(t);
   t->vi->enable_cursor();
   t->vi->move_cursor(t->r, t->c, get_curr_cell_color(t));

   if (t->vi->redraw_static_elements)
      t->vi->redraw_static_elements();

   if (t->vi->enable_static_elems_refresh)
      t->vi->enable_static_elems_refresh();
}

#include "term_action_wrappers.c.h"

#ifdef DEBUG

void debug_term_dump_font_table(term *t)
{
   static const u8 hex_digits[] = "0123456789abcdef";

   u8 color = make_color(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR);

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

term *alloc_term_struct(void)
{
   return kzmalloc(sizeof(term));
}

void free_term_struct(term *t)
{
   ASSERT(t != &first_instance);
   kfree2(t, sizeof(term));
}

void dispose_term(term *t)
{
   ASSERT(t != &first_instance);

   if (t->buffer) {
      kfree2(t->buffer, 2 * t->total_buffer_rows * t->cols);
      t->buffer = NULL;
   }

   if (t->term_tabs_buf) {
      kfree2(t->term_tabs_buf, t->cols * t->rows);
      t->term_tabs_buf = NULL;
   }
}

const video_interface *term_get_vi(term *t)
{
   return t->vi;
}

void set_curr_term(term *t)
{
   ASSERT(!is_preemption_enabled());

   term_pause_video_output(get_curr_term());
   __curr_term = t;
   term_restart_video_output(get_curr_term());
}

int
init_term(term *t, const video_interface *intf, u16 rows, u16 cols)
{
   ASSERT(t != &first_instance || !are_interrupts_enabled());

   if (kopt_serial_console) {
      intf = &no_output_vi;
   }

   t->tabsize = 8;
   t->cols = cols;
   t->rows = rows;
   t->saved_vi = intf;
   t->vi = (t == &first_instance) ? intf : &no_output_vi;

   ringbuf_init(&t->ringbuf,
                ARRAY_SIZE(t->actions_buf),
                sizeof(term_action),
                t->actions_buf);

   if (!in_panic() && !kopt_serial_console) {
      t->extra_buffer_rows = 9 * t->rows;
      t->total_buffer_rows = t->rows + t->extra_buffer_rows;

      if (is_kmalloc_initialized())
         t->buffer = kmalloc(2 * t->total_buffer_rows * t->cols);
   }

   if (t->buffer) {

      t->term_tabs_buf = kzmalloc(t->cols * t->rows);

      if (!t->term_tabs_buf) {

         if (t != &first_instance) {
            kfree2(t->buffer, 2 * t->total_buffer_rows * t->cols);
            return -ENOMEM;
         }

         printk("WARNING: unable to allocate term_tabs_buf\n");
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

      if (!in_panic() && !kopt_serial_console)
         printk("ERROR: unable to allocate the term buffer.\n");
   }

   t->vi->enable_cursor();
   term_action_move_ch_and_cur(t, 0, 0);

   for (u16 i = 0; i < t->rows; i++)
      ts_clear_row(t, i, make_color(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));

   t->initialized = true;
   return 0;
}
