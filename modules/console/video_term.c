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
#include <tilck/kernel/sync.h>

#include "video_term_int.h"

struct vterm {

   bool initialized;
   bool cursor_enabled;
   bool using_alt_buffer;

   struct kmutex lock;

   u16 tabsize;               /* term's current tab size */
   u16 rows;                  /* term's rows count */
   u16 cols;                  /* term's columns count */

   u16 col_offset;
   u16 r;                     /* current row */
   u16 c;                     /* current col */

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

   u16 main_scroll_region_start;
   u16 main_scroll_region_end;

   u16 alt_scroll_region_start;
   u16 alt_scroll_region_end;

   u16 *start_scroll_region;
   u16 *end_scroll_region;

   bool *tabs_buf;
   bool *main_tabs_buf;
   bool *alt_tabs_buf;

   struct safe_ringbuf ringb;
   struct term_action actions_buf[32];

   term_filter filter;
   void *filter_ctx;
};

static struct vterm first_instance;
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

/*
 * Performance note: using macros instead of ALWAYS_INLINE funcs because,
 * despite the forced inlining, even with -O3, the compiler optimizes better
 * this code when macros are used instead of inline funcs. Better means
 * a smaller binary.
 *
 * Measurements
 * --------------
 *
 * full debug build, -O0 -fno-inline-funcs EXCEPT the ALWAYS_INLINE ones
 * -----------------------------------------------------------------------
 *
 * Before (inline funcs):
 *    text     data      bss      dec    hex   filename
 *  466397    34104   316082   816583  c75c7   ./build/tilck
 *
 * After (with macros):
 *    text     data     bss      dec     hex   filename
 *  465421    34104  316082   815607   c71f7   ./build/tilck

 * full optimization, -O3, no debug checks:
 * -------------------------------------------
 *
 * Before (inline funcs):
 *
 *    text     data     bss      dec     hex   filename
 *  301608    29388  250610   581606   8dfe6   tilck
 *
 * After (with macros):
 *    text     data     bss      dec     hex   filename
 *  301288    29388  250610   581286   8dea6   tilck
 */

#define calc_buf_row(t, r) (((r) + (t)->scroll) % (t)->total_buffer_rows)
#define get_buf_row(t, r) (&(t)->buffer[calc_buf_row((t), (r)) * (t)->cols])
#define buf_set_entry(t, r, c, e) (get_buf_row((t), (r))[(c)] = (e))
#define buf_get_entry(t, r, c) (get_buf_row((t), (r))[(c)])
#define buf_get_char_at(t, r, c) (vgaentry_get_char(buf_get_entry((t),(r),(c))))

static void
buf_copy_row(term *_t, u32 dest, u32 src)
{
   struct vterm *const t = _t;

   if (UNLIKELY(dest == src))
      return;

   memcpy(get_buf_row(t, dest), get_buf_row(t, src), t->cols * 2);
}

static ALWAYS_INLINE bool ts_is_at_bottom(term *_t)
{
   struct vterm *const t = _t;
   return t->scroll == t->max_scroll;
}

static ALWAYS_INLINE u8 get_curr_cell_color(term *_t)
{
   struct vterm *const t = _t;

   if (!t->buffer)
      return 0;

   return vgaentry_get_color(buf_get_entry(t, t->r, t->c));
}

static void term_action_enable_cursor(term *_t, u16 val, ...)
{
   struct vterm *const t = _t;

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

static void term_redraw2(term *_t, u16 s, u16 e)
{
   struct vterm *const t = _t;

   if (!t->buffer)
      return;

   fpu_context_begin();
   {
      for (u16 row = s; row < e; row++)
         t->vi->set_row(row, get_buf_row(t, row), true);
   }

   fpu_context_end();
}

static inline void term_redraw(term *_t)
{
   struct vterm *const t = _t;
   term_redraw2(t, 0, t->rows);
}

static inline void term_redraw_scroll_region(term *_t)
{
   struct vterm *const t = _t;
   term_redraw2(t, *t->start_scroll_region, *t->end_scroll_region + 1);
}

static void ts_set_scroll(term *_t, u32 requested_scroll)
{
   struct vterm *const t = _t;

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

static ALWAYS_INLINE void ts_scroll_up(term *_t, u32 lines)
{
   struct vterm *const t = _t;

   if (lines > t->scroll)
      ts_set_scroll(t, 0);
   else
      ts_set_scroll(t, t->scroll - lines);
}

static ALWAYS_INLINE void ts_scroll_down(term *_t, u32 lines)
{
   struct vterm *const t = _t;
   ts_set_scroll(t, t->scroll + lines);
}

static ALWAYS_INLINE void ts_scroll_to_bottom(term *_t)
{
   struct vterm *const t = _t;
   if (t->scroll != t->max_scroll) {
      ts_set_scroll(t, t->max_scroll);
   }
}

static void ts_buf_clear_row(term *_t, u16 row, u8 color)
{
   struct vterm *const t = _t;

   if (!t->buffer)
      return;

   memset16(get_buf_row(t, row), make_vgaentry(' ', color), t->cols);
}

static void ts_clear_row(term *_t, u16 row, u8 color)
{
   struct vterm *const t = _t;
   ts_buf_clear_row(t, row, color);
   t->vi->clear_row(row, color);
}

/* ---------------- term actions --------------------- */

static void term_execute_action(struct vterm *t, struct term_action *a);

static void term_int_scroll_up(term *_t, u32 lines)
{
   struct vterm *const t = _t;

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

static void term_int_scroll_down(term *_t, u32 lines)
{
   struct vterm *const t = _t;

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
term_action_scroll(term *_t, u32 lines, enum term_scroll_type st, ...)
{
   struct vterm *const t = _t;

   if (st == term_scroll_up) {
      term_int_scroll_up(t, lines);
   } else {
      ASSERT(st == term_scroll_down);
      term_int_scroll_down(t, lines);
   }
}

static void term_action_non_buf_scroll_up(term *_t, u16 n, ...)
{
   struct vterm *const t = _t;
   const u16 sR = *t->start_scroll_region;
   const u16 eR = *t->end_scroll_region + 1;

   if (!t->buffer || !n)
      return;

   n = (u16)MIN(n, eR - sR);

   for (u32 row = sR; (int)row < eR - n; row++)
      buf_copy_row(t, row, row + n);

   for (u16 row = eR - n; row < eR; row++)
      ts_buf_clear_row(t, row, DEFAULT_COLOR16);

   term_redraw_scroll_region(t);
}

static void term_action_non_buf_scroll_down(term *_t, u16 n, ...)
{
   struct vterm *const t = _t;
   const u16 sR = *t->start_scroll_region;
   const u16 eR = *t->end_scroll_region + 1;

   if (!t->buffer || !n)
      return;

   n = (u16)MIN(n, t->rows);

   for (u32 row = eR - n; row > sR; row--)
      buf_copy_row(t, row - 1 + n, row - 1);

   for (u16 row = sR; row < n; row++)
      ts_buf_clear_row(t, row, DEFAULT_COLOR16);

   term_redraw_scroll_region(t);
}

static void term_action_non_buf_scroll(term *_t, u16 n, u16 dir, ...)
{
   struct vterm *const t = _t;

   if (dir == term_scroll_up) {

      term_action_non_buf_scroll_up(t, n);

   } else {

      ASSERT(dir == term_scroll_down);
      term_action_non_buf_scroll_down(t, n);
   }
}

static void term_action_move_ch_and_cur(term *_t, int row, int col, ...)
{
   struct vterm *const t = _t;

   if (!t->buffer)
      return;

   t->r = (u16) CLAMP(row, 0, t->rows - 1);
   t->c = (u16) CLAMP(col, 0, t->cols - 1);

   if (t->cursor_enabled)
      t->vi->move_cursor(t->r, t->c, get_curr_cell_color(t));

   if (t->vi->flush_buffers)
      t->vi->flush_buffers();
}

static void term_internal_incr_row(term *_t, u8 color)
{
   struct vterm *const t = _t;
   const u16 sR = *t->start_scroll_region;
   const u16 eR = *t->end_scroll_region + 1;

   t->col_offset = 0;

   if (t->r < eR - 1) {
      ++t->r;
      return;
   }

   if (sR || eR < t->rows) {

      term_action_move_ch_and_cur(t, t->r, 0);

      if (t->r == eR - 1)
         term_action_non_buf_scroll_up(t, 1);
      else if (t->r < t->rows - 1)
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

static void term_internal_write_printable_char(term *_t, u8 c, u8 color)
{
   struct vterm *const t = _t;
   const u16 entry = make_vgaentry(c, color);
   buf_set_entry(t, t->r, t->c, entry);
   t->vi->set_char_at(t->r, t->c, entry);
   t->c++;
}

static void term_internal_write_tab(term *_t, u8 color)
{
   struct vterm *const t = _t;
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

static void term_internal_write_backspace(term *_t, u8 color)
{
   struct vterm *const t = _t;
   if (!t->c || t->c <= t->col_offset)
      return;

   const u16 space_entry = make_vgaentry(' ', color);
   t->c--;

   if (!t->tabs_buf || !t->tabs_buf[t->r * t->cols + t->c]) {
      buf_set_entry(t, t->r, t->c, space_entry);
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

      if (buf_get_char_at(t, t->r, t->c - 1) != ' ')
         break;

      t->c--;
   }
}

static void term_internal_delete_last_word(term *_t, u8 color)
{
   struct vterm *const t = _t;
   u8 c;

   while (t->c > 0) {

      c = buf_get_char_at(t, t->r, t->c - 1);

      if (c != ' ')
         break;

      term_internal_write_backspace(t, color);
   }

   while (t->c > 0) {

      c = buf_get_char_at(t, t->r, t->c - 1);

      if (c == ' ')
         break;

      term_internal_write_backspace(t, color);
   }
}

static void term_internal_write_char2(term *_t, char c, u8 color)
{
   struct vterm *const t = _t;

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

static void term_action_write(term *_t, char *buf, u32 len, u8 color)
{
   struct vterm *const t = _t;
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
term_action_dwrite_no_filter(term *_t, char *buf, u32 len, u8 color)
{
   struct vterm *const t = _t;

   for (u32 i = 0; i < len; i++)
      term_internal_write_char2(t, buf[i], color);
}

static void term_action_set_col_offset(term *_t, u16 off, ...)
{
   struct vterm *const t = _t;
   t->col_offset = off;
}

static void term_action_move_ch_and_cur_rel(term *_t, s8 dr, s8 dc, ...)
{
   struct vterm *const t = _t;

   if (!t->buffer)
      return;

   t->r = (u16) CLAMP((int)t->r + dr, 0, t->rows - 1);
   t->c = (u16) CLAMP((int)t->c + dc, 0, t->cols - 1);

   if (t->cursor_enabled)
      t->vi->move_cursor(t->r, t->c, get_curr_cell_color(t));

   if (t->vi->flush_buffers)
      t->vi->flush_buffers();
}

static void term_action_reset(term *_t, ...)
{
   struct vterm *const t = _t;

   t->vi->enable_cursor();
   term_action_move_ch_and_cur(t, 0, 0);
   t->scroll = t->max_scroll = 0;

   for (u16 i = 0; i < t->rows; i++)
      ts_clear_row(t, i, DEFAULT_COLOR16);

   if (t->tabs_buf)
      memset(t->tabs_buf, 0, t->cols * t->rows);
}

static void term_action_erase_in_display(term *_t, int mode, ...)
{
   struct vterm *const t = _t;
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

   if (t->vi->flush_buffers)
      t->vi->flush_buffers();
}

static void term_action_erase_in_line(term *_t, int mode, ...)
{
   struct vterm *const t = _t;
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

   if (t->vi->flush_buffers)
      t->vi->flush_buffers();
}

static void
term_action_del(term *_t, enum term_del_type del_type, int m, ...)
{
   struct vterm *const t = _t;

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

static void term_action_pause_video_output(term *_t, ...)
{
   struct vterm *const t = _t;

   if (t->vi->disable_static_elems_refresh)
      t->vi->disable_static_elems_refresh();

   t->vi->disable_cursor();
   t->saved_vi = t->vi;
   t->vi = &no_output_vi;
}

static void term_action_restart_video_output(term *_t, ...)
{
   struct vterm *const t = _t;
   t->vi = t->saved_vi;

   term_redraw(t);
   term_action_enable_cursor(t, t->cursor_enabled);

   if (t->vi->redraw_static_elements)
      t->vi->redraw_static_elements();

   if (t->vi->enable_static_elems_refresh)
      t->vi->enable_static_elems_refresh();
}

static int
term_allocate_alt_buffers(term *_t)
{
   struct vterm *const t = _t;
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
term_action_use_alt_buffer(term *_t, bool use_alt_buffer, ...)
{
   struct vterm *const t = _t;
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
   term_action_enable_cursor(t, t->cursor_enabled);
}

static void
term_action_ins_blank_lines(term *_t, u32 n)
{
   struct vterm *const t = _t;
   const u16 eR = *t->end_scroll_region + 1;

   if (!t->buffer || !n)
      return;

   if (t->r >= eR)
      return; /* we're outside the scrolling region: do nothing */

   t->c = 0;
   n = MIN(n, (u32)(eR - t->r));

   for (u32 row = eR - n - 1; row >= t->r; row--)
      buf_copy_row(t, row - 1 + n, row - 1);

   for (u16 row = t->r; row < t->r + n; row++)
      ts_buf_clear_row(t, row, DEFAULT_COLOR16);

   term_redraw_scroll_region(t);
}

static void
term_action_del_lines(term *_t, u32 n)
{
   struct vterm *const t = _t;
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

static void
term_action_set_scroll_region(term *_t, u16 start, u16 end)
{
   struct vterm *const t = _t;
   start = (u16) CLAMP(start, 0u, t->rows - 1u);
   end = (u16) CLAMP(end, 0u, t->rows - 1u);

   if (start >= end)
      return;

   *t->start_scroll_region = start;
   *t->end_scroll_region = end;
   term_action_move_ch_and_cur(t, 0, 0);
}

#include "term_action_wrappers.c.h"

#ifdef DEBUG

static void
debug_term_dump_font_table(term *_t)
{
   struct vterm *const t = _t;
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

static term *
alloc_term_struct(void)
{
   return kzmalloc(sizeof(struct vterm));
}

static void
free_term_struct(term *_t)
{
   struct vterm *const t = _t;
   ASSERT(t != &first_instance);
   kfree2(t, sizeof(struct vterm));
}

static void
dispose_term(term *_t)
{
   struct vterm *const t = _t;
   ASSERT(t != &first_instance);

   kmutex_destroy(&t->lock);

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
vterm_get_params(term *_t, struct term_params *out)
{
   struct vterm *const t = _t;

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
init_vterm(term *_t,
           const struct video_interface *intf,
           u16 rows,
           u16 cols,
           int rows_buf)
{
   struct vterm *const t = _t;
   ASSERT(t != &first_instance || !are_interrupts_enabled());

   kmutex_init(&t->lock, 0);
   t->tabsize = 8;
   t->cols = cols;
   t->rows = rows;
   t->saved_vi = intf;
   t->vi = (t == &first_instance) ? intf : &no_output_vi;

   t->main_scroll_region_start = t->alt_scroll_region_start = 0;
   t->main_scroll_region_end = t->alt_scroll_region_end = t->rows - 1;

   t->start_scroll_region = &t->main_scroll_region_start;
   t->end_scroll_region = &t->main_scroll_region_end;

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

static term *vterm_get_first_inst(void)
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
