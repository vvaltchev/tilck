/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/mod_console.h>

#include <tilck/common/color_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/hal.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/term_aux.h>
#include <tilck/kernel/safe_ringbuf.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/interrupts.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/errno.h>

#include "video_term_int.h"

struct vterm {

   bool initialized;
   bool cursor_enabled;
   bool using_alt_buffer;

   struct term_rb_data rb_data;

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

   struct term_action actions_buf[32];

   term_filter filter;
   void *filter_ctx;
};

static struct vterm first_instance;
static u16 failsafe_buffer[80 * 25];

/* ------------ No-output video-interface ------------------ */

static void no_vi_set_char_at(u16 row, u16 col, u16 entry) { }
static void no_vi_set_row(u16 row, u16 *data, bool fpu_allowed) { }
static void no_vi_clear_row(u16 row_num, u8 color) { }
static void no_vi_move_cursor(u16 row, u16 col, int color) { }
static void no_vi_enable_cursor(void) { }
static void no_vi_disable_cursor(void) { }
static void no_vi_scroll_one_line_up(void) { }
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
buf_copy_row(struct vterm *t, u32 dest, u32 src)
{
   if (UNLIKELY(dest == src))
      return;

   memcpy(get_buf_row(t, dest), get_buf_row(t, src), t->cols * 2);
}

static ALWAYS_INLINE bool ts_is_at_bottom(struct vterm *t)
{
   return t->scroll == t->max_scroll;
}

static ALWAYS_INLINE u8 get_curr_cell_color(struct vterm *t)
{
   if (!t->buffer)
      return 0;

   return vgaentry_get_color(buf_get_entry(t, t->r, t->c));
}

static ALWAYS_INLINE u8 get_curr_cell_fg_color(struct vterm *t)
{
   if (!t->buffer)
      return 0;

   return vgaentry_get_fg(buf_get_entry(t, t->r, t->c));
}

static void term_int_enable_cursor(struct vterm *t, bool val)
{
   if (val == 0) {

      t->vi->disable_cursor();
      t->cursor_enabled = false;

   } else {

      ASSERT(val == 1);
      t->vi->enable_cursor();
      t->vi->move_cursor(t->r, t->c, get_curr_cell_fg_color(t));
      t->cursor_enabled = true;
   }
}

static void term_redraw2(struct vterm *t, u16 s, u16 e)
{
   const bool fpu_allowed = !in_irq() && !in_panic();

   if (!t->buffer)
      return;

   if (fpu_allowed)
      fpu_context_begin();

   for (u16 row = s; row < e; row++)
      t->vi->set_row(row, get_buf_row(t, row), fpu_allowed);

   if (fpu_allowed)
      fpu_context_end();
}

static inline void term_redraw(struct vterm *t)
{
   term_redraw2(t, 0, t->rows);
}

static inline void term_redraw_scroll_region(struct vterm *t)
{
   term_redraw2(t, *t->start_scroll_region, *t->end_scroll_region + 1);
}

static void ts_set_scroll(struct vterm *t, u32 requested_scroll)
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

static ALWAYS_INLINE void ts_scroll_up(struct vterm *t, u32 lines)
{
   if (lines > t->scroll)
      ts_set_scroll(t, 0);
   else
      ts_set_scroll(t, t->scroll - lines);
}

static ALWAYS_INLINE void ts_scroll_down(struct vterm *t, u32 lines)
{
   ts_set_scroll(t, t->scroll + lines);
}

static ALWAYS_INLINE void ts_scroll_to_bottom(struct vterm *t)
{
   if (t->scroll != t->max_scroll) {
      ts_set_scroll(t, t->max_scroll);
   }
}

static void ts_buf_clear_row(struct vterm *t, u16 row, u8 color)
{
   if (!t->buffer)
      return;

   memset16(get_buf_row(t, row), make_vgaentry(' ', color), t->cols);
}

static void ts_clear_row(struct vterm *t, u16 row, u8 color)
{
   ts_buf_clear_row(t, row, color);
   t->vi->clear_row(row, color);
}

static void term_int_scroll_up(struct vterm *t, u32 lines)
{
   ts_scroll_up(t, lines);

   if (t->cursor_enabled) {

      if (!ts_is_at_bottom(t)) {

         t->vi->disable_cursor();

      } else {

         t->vi->enable_cursor();
         t->vi->move_cursor(t->r, t->c, get_curr_cell_fg_color(t));
      }
   }
}

static void term_int_scroll_down(struct vterm *t, u32 lines)
{
   ts_scroll_down(t, lines);

   if (t->cursor_enabled) {
      if (ts_is_at_bottom(t)) {
         t->vi->enable_cursor();
         t->vi->move_cursor(t->r, t->c, get_curr_cell_fg_color(t));
      }
   }
}

static void term_internal_non_buf_scroll_up(struct vterm *t, u16 n)
{
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

static void term_internal_non_buf_scroll_down(struct vterm *t, u16 n)
{
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

static void term_int_move_cur(struct vterm *t, int row, int col)
{
   if (!t->buffer)
      return;

   t->r = (u16) CLAMP(row, 0, t->rows - 1);
   t->c = (u16) CLAMP(col, 0, t->cols - 1);

   if (t->cursor_enabled)
      t->vi->move_cursor(t->r, t->c, get_curr_cell_fg_color(t));
}

static void term_internal_incr_row(struct vterm *t)
{
   const u16 sR = *t->start_scroll_region;
   const u16 eR = *t->end_scroll_region + 1;

   t->col_offset = 0;

   if (t->r < eR - 1) {
      ++t->r;
      return;
   }

   if (sR || eR < t->rows) {

      term_int_move_cur(t, t->r, 0);

      if (t->r == eR - 1)
         term_internal_non_buf_scroll_up(t, 1);
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

   ts_clear_row(t, t->rows - 1, DEFAULT_COLOR16);
}

static void term_internal_write_printable_char(struct vterm *t, u8 c, u8 color)
{
   const u16 entry = make_vgaentry(c, color);
   buf_set_entry(t, t->r, t->c, entry);
   t->vi->set_char_at(t->r, t->c, entry);
   t->c++;
}

static void term_internal_write_tab(struct vterm *t, u8 color)
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

static void term_internal_write_backspace(struct vterm *t, u8 color)
{
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

static void term_internal_delete_last_word(struct vterm *t, u8 color)
{
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

static void term_internal_write_char2(struct vterm *t, char c, u8 color)
{
   switch (c) {

      case '\n':
         term_internal_incr_row(t);
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
            term_internal_incr_row(t);
         }

         term_internal_write_printable_char(t, (u8)c, color);
         break;
   }
}

static int
term_allocate_alt_buffers(struct vterm *t)
{
   t->screen_buf_copy = kalloc_array_obj(u16, t->rows * t->cols);

   if (!t->screen_buf_copy)
      return -ENOMEM;

   if (!t->alt_tabs_buf) {

      t->alt_tabs_buf = kzmalloc(t->rows * t->cols);

      if (!t->alt_tabs_buf) {
         kfree_array_obj(t->screen_buf_copy, u16, t->rows * t->cols);
         t->screen_buf_copy = NULL;
         return -ENOMEM;
      }
   }

   return 0;
}

static void term_execute_action(struct vterm *t, struct term_action *a);

#include "term_actions.c.h"
#include "term_action_wrappers.c.h"

#if DEBUG_CHECKS

static void
debug_term_dump_font_table(term *_t)
{
   struct vterm *const t = _t;
   static const u8 hex_digits[] = "0123456789abcdef";
   const u8 color = DEFAULT_COLOR16;

   term_internal_incr_row(t);
   t->c = 0;

   for (u32 i = 0; i < 6; i++)
      term_internal_write_printable_char(t, ' ', color);

   for (u32 i = 0; i < 16; i++) {
      term_internal_write_printable_char(t, hex_digits[i], color);
      term_internal_write_printable_char(t, ' ', color);
   }

   term_internal_incr_row(t);
   term_internal_incr_row(t);
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

      term_internal_incr_row(t);
      t->c = 0;
   }

   term_internal_incr_row(t);
   t->c = 0;
}

#endif

static term *
alloc_term_struct(void)
{
   return kzalloc_obj(struct vterm);
}

static void
free_term_struct(term *_t)
{
   struct vterm *const t = _t;
   ASSERT(t != &first_instance);
   kfree_obj(t, struct vterm);
}

static void
dispose_term(term *_t)
{
   struct vterm *const t = _t;
   ASSERT(t != &first_instance);

   dispose_term_rb_data(&t->rb_data);

   if (t->buffer) {
      kfree_array_obj(t->buffer, u16, t->total_buffer_rows * t->cols);
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
      kfree_array_obj(t->screen_buf_copy, u16, t->rows * t->cols);
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

   t->tabsize = 8;

   if (intf) {

      t->cols = cols;
      t->rows = rows;
      t->saved_vi = intf;
      t->vi = (t == &first_instance) ? intf : &no_output_vi;

   } else {

      t->cols = 80;
      t->rows = 25;
      t->saved_vi = &no_output_vi;
      t->vi = &no_output_vi;
   }

   t->main_scroll_region_start = t->alt_scroll_region_start = 0;
   t->main_scroll_region_end = t->alt_scroll_region_end = t->rows - 1;

   t->start_scroll_region = &t->main_scroll_region_start;
   t->end_scroll_region = &t->main_scroll_region_end;

   init_term_rb_data(&t->rb_data,
                     ARRAY_SIZE(t->actions_buf),
                     sizeof(struct term_action),
                     t->actions_buf);

   if (!in_panic() && intf) {

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

      if (!in_panic() && intf)
         printk("ERROR: unable to allocate the term buffer.\n");
   }

   for (u16 i = 0; i < t->rows; i++)
      ts_clear_row(t, i, DEFAULT_COLOR16);

   t->cursor_enabled = true;
   t->vi->enable_cursor();
   term_int_move_cur(t, 0, 0);
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
   .pause_output = vterm_pause_output,
   .restart_output = vterm_restart_output,
   .set_filter = vterm_set_filter,

   .get_first_term = vterm_get_first_inst,
   .video_term_init = init_vterm,
   .serial_term_init = NULL,
   .alloc = alloc_term_struct,
   .free = free_term_struct,
   .dispose = dispose_term,

#if DEBUG_CHECKS
   .debug_dump_font_table = debug_term_dump_font_table,
#endif
};

__attribute__((constructor))
static void register_term(void)
{
   register_term_intf(&intf);
}
