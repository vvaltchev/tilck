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

#define _TERM_C_

#include "term_int.h"

struct term {

   bool initialized;
   int tabsize;
   u16 cols;
   u16 rows;

   u16 current_row;
   u16 current_col;

   /* TODO: move term's state here */
};

static term first_instance;

static u16 term_col_offset;

static const video_interface *vi;
static const video_interface *saved_vi;

static u16 *buffer;
static u32 scroll;
static u32 max_scroll;
static u32 total_buffer_rows;
static u32 extra_buffer_rows;
static u16 failsafe_buffer[80 * 25];
static bool *term_tabs;

static ringbuf term_ringbuf;
static term_action term_actions_buf[32];

static term_filter_func filter;
static void *filter_ctx;

term *__curr_term = &first_instance;

/* ------------ No-output video-interface ------------------ */

void no_vi_set_char_at(int row, int col, u16 entry) { }
void no_vi_set_row(int row, u16 *data, bool flush) { }
void no_vi_clear_row(int row_num, u8 color) { }
void no_vi_move_cursor(int row, int col, int color) { }
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

static ALWAYS_INLINE void buffer_set_entry(term *t, int row, int col, u16 e)
{
   buffer[(row + scroll) % total_buffer_rows * t->cols + col] = e;
}

static ALWAYS_INLINE u16 buffer_get_entry(term *t, int row, int col)
{
   return buffer[(row + scroll) % total_buffer_rows * t->cols + col];
}

static ALWAYS_INLINE bool ts_is_at_bottom(void)
{
   return scroll == max_scroll;
}

static void term_redraw(term *t)
{
   fpu_context_begin();

   for (u32 row = 0; row < t->rows; row++) {
      u32 buffer_row = (scroll + row) % total_buffer_rows;
      vi->set_row(row, &buffer[t->cols * buffer_row], true);
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
      max_scroll > extra_buffer_rows
         ? max_scroll - extra_buffer_rows
         : 0;

   requested_scroll = MIN(MAX(requested_scroll, min_scroll), max_scroll);

   if (requested_scroll == scroll)
      return; /* nothing to do */

   scroll = requested_scroll;
   term_redraw(t);
}

static ALWAYS_INLINE void ts_scroll_up(term *t, u32 lines)
{
   if (lines > scroll)
      ts_set_scroll(t, 0);
   else
      ts_set_scroll(t, scroll - lines);
}

static ALWAYS_INLINE void ts_scroll_down(term *t, u32 lines)
{
   ts_set_scroll(t, scroll + lines);
}

static ALWAYS_INLINE void ts_scroll_to_bottom(term *t)
{
   if (scroll != max_scroll) {
      ts_set_scroll(t, max_scroll);
   }
}

static void ts_buf_clear_row(term *t, int row, u8 color)
{
   u16 *rowb = buffer + t->cols * ((row + scroll) % total_buffer_rows);
   memset16(rowb, make_vgaentry(' ', color), t->cols);
}

static void ts_clear_row(term *t, int row, u8 color)
{
   ts_buf_clear_row(t, row, color);
   vi->clear_row(row, color);
}

/* ---------------- term actions --------------------- */

static void term_execute_action(term *t, term_action *a);

static void term_int_scroll_up(term *t, u32 lines)
{
   ts_scroll_up(t, lines);

   if (!ts_is_at_bottom()) {
      vi->disable_cursor();
   } else {
      vi->enable_cursor();
      vi->move_cursor(t->current_row,
                      t->current_col,
                      vgaentry_get_color(buffer_get_entry(t, t->current_row,
                                                          t->current_col)));
   }

   if (vi->flush_buffers)
      vi->flush_buffers();
}

static void term_int_scroll_down(term *t, u32 lines)
{
   ts_scroll_down(t, lines);

   if (ts_is_at_bottom()) {
      vi->enable_cursor();
      vi->move_cursor(t->current_row,
                      t->current_col,
                      vgaentry_get_color(buffer_get_entry(t, t->current_row,
                                                          t->current_col)));
   }

   if (vi->flush_buffers)
      vi->flush_buffers();
}

static void term_action_scroll(term *t, int lines)
{
   if (lines > 0)
      term_int_scroll_up(t, lines);
   else
      term_int_scroll_down(t, -lines);
}

static void term_internal_incr_row(term *t, u8 color)
{
   term_col_offset = 0;

   if (t->current_row < t->rows - 1) {
      ++t->current_row;
      return;
   }

   max_scroll++;

   if (vi->scroll_one_line_up) {
      scroll++;
      vi->scroll_one_line_up();
   } else {
      ts_set_scroll(t, max_scroll);
   }

   ts_clear_row(t, t->rows - 1, color);
}

static void term_internal_write_printable_char(term *t, u8 c, u8 color)
{
   u16 entry = make_vgaentry(c, color);
   buffer_set_entry(t, t->current_row, t->current_col, entry);
   vi->set_char_at(t->current_row, t->current_col, entry);
   t->current_col++;
}

static void term_internal_write_tab(term *t, u8 color)
{
   int rem = t->cols - t->current_col - 1;

   if (!term_tabs) {

      if (rem)
         term_internal_write_printable_char(t, ' ', color);

      return;
   }

   int tab_col = MIN(round_up_at(t->current_col+1, t->tabsize), (u32)t->cols - 2);
   term_tabs[t->current_row * t->cols + tab_col] = 1;
   t->current_col = tab_col + 1;
}

void term_internal_write_backspace(term *t, u8 color)
{
   if (!t->current_col || t->current_col <= term_col_offset)
      return;

   const u16 space_entry = make_vgaentry(' ', color);
   t->current_col--;

   if (!term_tabs || !term_tabs[t->current_row * t->cols + t->current_col]) {
      buffer_set_entry(t, t->current_row, t->current_col, space_entry);
      vi->set_char_at(t->current_row, t->current_col, space_entry);
      return;
   }

   /* we hit the end of a tab */
   term_tabs[t->current_row * t->cols + t->current_col] = 0;

   for (int i = t->tabsize - 1; i >= 0; i--) {

      if (!t->current_col || t->current_col == term_col_offset)
         break;

      if (term_tabs[t->current_row * t->cols + t->current_col - 1])
         break; /* we hit the previous tab */

      if (i)
         t->current_col--;
   }
}

static void term_serial_con_write(char c)
{
   serial_write(COM1, c);
}

void term_internal_write_char2(term *t, char c, u8 color)
{
   if (kopt_serial_mode == TERM_SERIAL_CONSOLE) {
      serial_write(COM1, c);
      return;
   }

   switch (c) {

      case '\n':
         term_internal_incr_row(t, color);
         break;

      case '\r':
         t->current_col = 0;
         break;

      case '\t':
         term_internal_write_tab(t, color);
         break;

      default:

         if (t->current_col == t->cols) {
            t->current_col = 0;
            term_internal_incr_row(t, color);
         }

         term_internal_write_printable_char(t, c, color);
         break;
   }
}

static void term_action_write(term *t, char *buf, u32 len, u8 color)
{
   ts_scroll_to_bottom(t);
   vi->enable_cursor();

   for (u32 i = 0; i < len; i++) {

      if (filter) {

         term_action a = { .type1 = a_none };

         if (filter((u8) buf[i], &color, &a, filter_ctx))
            term_internal_write_char2(t, buf[i], color);

         if (a.type1 != a_none)
            term_execute_action(t, &a);

      } else {
         term_internal_write_char2(t, buf[i], color);
      }

   }

   vi->move_cursor(t->current_row,
                   t->current_col,
                   vgaentry_get_color(buffer_get_entry(t, t->current_row,
                                                       t->current_col)));

   if (vi->flush_buffers)
      vi->flush_buffers();
}

static void term_action_set_col_offset(term *t, u32 off)
{
   term_col_offset = off;
}

static void term_action_move_ch_and_cur(term *t, int row, int col)
{
   t->current_row = MIN(MAX(row, 0), t->rows - 1);
   t->current_col = MIN(MAX(col, 0), t->cols - 1);

   vi->move_cursor(t->current_row,
                   t->current_col,
                   vgaentry_get_color(buffer_get_entry(t, t->current_row,
                                                       t->current_col)));


   if (vi->flush_buffers)
      vi->flush_buffers();
}

static void term_action_move_ch_and_cur_rel(term *t, s8 dx, s8 dy)
{
   t->current_row = MIN(MAX((int)t->current_row + dx, 0), t->rows - 1);
   t->current_col = MIN(MAX((int)t->current_col + dy, 0), t->cols - 1);

   vi->move_cursor(t->current_row,
                   t->current_col,
                   vgaentry_get_color(buffer_get_entry(t, t->current_row,
                                                       t->current_col)));


   if (vi->flush_buffers)
      vi->flush_buffers();
}

static void term_action_reset(term *t)
{
   vi->enable_cursor();
   term_action_move_ch_and_cur(t, 0, 0);
   scroll = max_scroll = 0;

   for (int i = 0; i < t->rows; i++)
      ts_clear_row(t, i, make_color(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));

   if (term_tabs)
      memset(term_tabs, 0, t->cols * t->rows);
}

static void term_action_erase_in_display(term *t, int mode)
{
   static const u16 entry =
      make_vgaentry(' ', make_color(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));

   switch (mode) {

      case 0:

         /* Clear the screen from the cursor position up to the end */

         for (u32 col = t->current_col; col < t->cols; col++) {
            buffer_set_entry(t, t->current_row, col, entry);
            vi->set_char_at(t->current_row, col, entry);
         }

         for (u32 i = t->current_row + 1; i < t->rows; i++)
            ts_clear_row(t, i, make_color(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));

         break;

      case 1:

         /* Clear the screen from the beginning up to cursor's position */

         for (u32 i = 0; i < t->current_row; i++)
            ts_clear_row(t, i, make_color(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));

         for (u32 col = 0; col < t->current_col; col++) {
            buffer_set_entry(t, t->current_row, col, entry);
            vi->set_char_at(t->current_row, col, entry);
         }

         break;

      case 2:

         /* Clear the whole screen */

         for (u32 i = 0; i < t->rows; i++)
            ts_clear_row(t, i, make_color(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));

         break;

      case 3:
         /* Clear the whole screen and erase the scroll buffer */
         {
            u32 row = t->current_row;
            u32 col = t->current_col;
            term_action_reset(t);
            vi->move_cursor(row, col, make_color(DEFAULT_FG_COLOR,
                                                 DEFAULT_BG_COLOR));
         }
         break;

      default:
         return;
   }

   if (vi->flush_buffers)
      vi->flush_buffers();
}

static void term_action_erase_in_line(term *t, int mode)
{
   static const u16 entry =
      make_vgaentry(' ', make_color(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));

   switch (mode) {

      case 0:
         for (u32 col = t->current_col; col < t->cols; col++) {
            buffer_set_entry(t, t->current_row, col, entry);
            vi->set_char_at(t->current_row, col, entry);
         }
         break;

      case 1:
         for (u32 col = 0; col < t->current_col; col++) {
            buffer_set_entry(t, t->current_row, col, entry);
            vi->set_char_at(t->current_row, col, entry);
         }
         break;

      case 2:
         ts_clear_row(t, t->current_row, vgaentry_get_color(entry));
         break;

      default:
         return;
   }

   if (vi->flush_buffers)
      vi->flush_buffers();
}

static void term_action_non_buf_scroll_up(term *t, u32 n)
{
   ASSERT(n >= 1);
   n = MIN(n, t->rows);

   for (u32 row = 0; row < t->rows - n; row++) {
      u32 s = (scroll + row + n) % total_buffer_rows;
      u32 d = (scroll + row) % total_buffer_rows;
      memcpy(&buffer[t->cols * d], &buffer[t->cols * s], t->cols * 2);
   }

   for (u32 row = t->rows - n; row < t->rows; row++)
      ts_buf_clear_row(t, row, make_color(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));

   term_redraw(t);
}

static void term_action_non_buf_scroll_down(term *t, u32 n)
{
   ASSERT(n >= 1);
   n = MIN(n, t->rows);

   for (int row = t->rows - n - 1; row >= 0; row--) {
      u32 s = (scroll + row) % total_buffer_rows;
      u32 d = (scroll + row + n) % total_buffer_rows;
      memcpy(&buffer[t->cols * d], &buffer[t->cols * s], t->cols * 2);
   }

   for (u32 row = 0; row < n; row++)
      ts_buf_clear_row(t, row, make_color(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));

   term_redraw(t);
}

static void term_action_pause_video_output(term *t)
{
   if (vi->disable_static_elems_refresh)
      vi->disable_static_elems_refresh();

   vi->disable_cursor();
   saved_vi = vi;
   vi = &no_output_vi;
}

static void term_action_restart_video_output(term *t)
{
   vi = saved_vi;

   term_redraw(t);
   vi->enable_cursor();

   if (vi->redraw_static_elements)
      vi->redraw_static_elements();

   if (vi->enable_static_elems_refresh)
      vi->enable_static_elems_refresh();
}

#include "term_action_wrappers.c.h"

#ifdef DEBUG

void debug_term_dump_font_table(term *t)
{
   static const char hex_digits[] = "0123456789abcdef";

   u8 color = make_color(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR);

   term_internal_incr_row(t, color);
   t->current_col = 0;

   for (u32 i = 0; i < 6; i++)
      term_internal_write_printable_char(t, ' ', color);

   for (u32 i = 0; i < 16; i++) {
      term_internal_write_printable_char(t, hex_digits[i], color);
      term_internal_write_printable_char(t, ' ', color);
   }

   term_internal_incr_row(t, color);
   term_internal_incr_row(t, color);
   t->current_col = 0;

   for (u32 i = 0; i < 16; i++) {

      term_internal_write_printable_char(t, '0', color);
      term_internal_write_printable_char(t, 'x', color);
      term_internal_write_printable_char(t, hex_digits[i], color);

      for (u32 i = 0; i < 3; i++)
         term_internal_write_printable_char(t, ' ', color);

      for (u32 j = 0; j < 16; j++) {

         u8 c = i * 16 + j;
         term_internal_write_printable_char(t, c, color);
         term_internal_write_printable_char(t, ' ', color);
      }

      term_internal_incr_row(t, color);
      t->current_col = 0;
   }

   term_internal_incr_row(t, color);
   t->current_col = 0;
}

#endif


void
init_term(term *t, const video_interface *intf, int rows, int cols)
{
   ASSERT(!are_interrupts_enabled());

   t->tabsize = 8;

   vi = intf;
   t->cols = cols;
   t->rows = rows;

   ringbuf_init(&term_ringbuf,
                ARRAY_SIZE(term_actions_buf),
                sizeof(term_action),
                term_actions_buf);

   if (!in_panic()) {
      extra_buffer_rows = 9 * t->rows;
      total_buffer_rows = t->rows + extra_buffer_rows;

      if (is_kmalloc_initialized())
         buffer = kmalloc(2 * total_buffer_rows * t->cols);
   }

   if (buffer) {

      term_tabs = kzmalloc(t->cols * t->rows);

      if (!term_tabs)
         printk("WARNING: unable to allocate the term_tabs buffer\n");

   } else {

      /* We're in panic or we were unable to allocate the buffer */
      t->cols = MIN(80, t->cols);
      t->rows = MIN(25, t->rows);

      extra_buffer_rows = 0;
      total_buffer_rows = t->rows;
      buffer = failsafe_buffer;

      if (!in_panic())
         printk("ERROR: unable to allocate the term buffer.\n");
   }

   vi->enable_cursor();
   term_action_move_ch_and_cur(t, 0, 0);

   for (int i = 0; i < t->rows; i++)
      ts_clear_row(t, i, make_color(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));

   t->initialized = true;
   printk_flush_ringbuf();
}
