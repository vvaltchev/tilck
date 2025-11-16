/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/term.h>

struct vterm;

enum term_scroll_type {
   term_scroll_up = 0,
   term_scroll_down = 1,
};

enum term_action_type {

   a_none,
   a_write,
   a_direct_write,               // [1]
   a_del_generic,
   a_scroll,                     // [2]
   a_set_col_offset,
   a_move_cur,
   a_move_cur_rel,
   a_reset,
   a_pause_output,
   a_restart_output,
   a_enable_cursor,
   a_use_alt_buffer,
   a_non_buf_scroll,             // [3]
   a_insert_blank_lines,
   a_delete_lines,
   a_set_scroll_region,
   a_insert_blank_chars,
   a_simple_del_chars,
   a_simple_erase_chars,
};

/*
 * NOTES [enum term_action_type].
 *
 *    [1] direct write without filters/move_cursor/flush
 *
 *    [2] arg1 = rows, arg2 = direction (0 = up, 1 = down)
 *          WARNING: up => text moves down, down => text moves up.
 *
 *    [3] non_buf scroll: (arg1: rows, arg2: direction (term_scroll_type))
 *          WARNING: up and down are inverted if compared to a_scroll.
 *              up => text moves up => new blank lines at the bottom
 *              down => text moves down => new blank lines at the top
 *
 *          REASON: because the `CSI n S` sequence is called SU (Scroll Up) and
 *             the `CSI n T` sequence is called SD (Scroll Down), despite what
 *             traditionally up and down mean when it's about scrolling.
 */

enum term_del_type {

   TERM_DEL_PREV_CHAR,
   TERM_DEL_PREV_WORD,
   TERM_DEL_ERASE_IN_DISPLAY,
   TERM_DEL_ERASE_IN_LINE,
};

/* --- interface exposed to the term filter func --- */

struct term_action {

   union {

      struct {
         u32 type1 :  8;
         u32 arg   : 24;
      };

      struct {
         u32 type2 :  8;
         u32 arg1  : 12;
         u32 arg2  : 12;
      };

      struct {
         u32 type3 :  8;
         u32 len   : 16;
         u32 col   :  8;
         void *ptr;
      };

      struct {
         u32 type4 :  8;
         u32 unused: 24;

         u16 arg16_1;
         u16 arg16_2;
      };
   };
};

STATIC_ASSERT(sizeof(struct term_action) == (2 * sizeof(ulong)));

u16 vterm_get_curr_row(struct vterm *t);
u16 vterm_get_curr_col(struct vterm *t);

static ALWAYS_INLINE void
term_make_action_write(struct term_action *a,
                       const char *buf,
                       size_t len,
                       u8 color)
{
   *a = (struct term_action) {
      .type3 = a_write,
      .len = UNSAFE_MIN((u32)len, MAX_TERM_WRITE_LEN),
      .col = color,
      .ptr = (void *)buf,
   };
}

static ALWAYS_INLINE void
term_make_action_direct_write(struct term_action *a,
                              const char *buf,
                              size_t len,
                              u8 color)
{
   *a = (struct term_action) {
      .type3 = a_direct_write,
      .len = UNSAFE_MIN((u32)len, MAX_TERM_WRITE_LEN),
      .col = color,
      .ptr = (void *)buf,
   };
}

static ALWAYS_INLINE void
term_make_action_del_prev_char(struct term_action *a)
{
   *a = (struct term_action) {
      .type1 = a_del_generic,
      .arg = TERM_DEL_PREV_CHAR,
   };
}

static ALWAYS_INLINE void
term_make_action_del_prev_word(struct term_action *a)
{
   *a = (struct term_action) {
      .type1 = a_del_generic,
      .arg = TERM_DEL_PREV_WORD,
   };
}

static ALWAYS_INLINE void
term_make_action_erase_in_display(struct term_action *a, u32 mode)
{
   *a = (struct term_action) {
      .type2 = a_del_generic,
      .arg1 = TERM_DEL_ERASE_IN_DISPLAY,
      .arg2 = mode,
   };
}

static ALWAYS_INLINE void
term_make_action_erase_in_line(struct term_action *a, u32 mode)
{
   *a = (struct term_action) {
      .type2 = a_del_generic,
      .arg1 = TERM_DEL_ERASE_IN_LINE,
      .arg2 = mode,
   };
}

static ALWAYS_INLINE void
term_make_action_scroll(struct term_action *a,
                        enum term_scroll_type st,
                        u32 rows)
{
   *a = (struct term_action) {
      .type2 = a_scroll,
      .arg1 = rows,
      .arg2 = st,
   };
}

static ALWAYS_INLINE void
term_make_action_set_col_off(struct term_action *a, u32 off)
{
   *a = (struct term_action) {
      .type1 = a_set_col_offset,
      .arg = off,
   };
}

static ALWAYS_INLINE void
term_make_action_move_cursor(struct term_action *a, u32 row, u32 col)
{
   *a = (struct term_action) {
      .type4 = a_move_cur,
      .arg16_1 = LO_BITS(row, 16, u16),
      .arg16_2 = LO_BITS(col, 16, u16),
   };
}

static ALWAYS_INLINE void
term_make_action_move_cursor_rel(struct term_action *a, int dr, int dc)
{
   *a = (struct term_action) {
      .type4 = a_move_cur_rel,
      .arg16_1 = LO_BITS((u32)dr, 16, u16),
      .arg16_2 = LO_BITS((u32)dc, 16, u16),
   };
}

static ALWAYS_INLINE void
term_make_action_reset(struct term_action *a)
{
   *a = (struct term_action) {
      .type1 = a_reset,
   };
}

static ALWAYS_INLINE void
term_make_action_pause_output(struct term_action *a)
{
   *a = (struct term_action) {
      .type1 = a_pause_output,
      .arg = 0,
   };
}

static ALWAYS_INLINE void
term_make_action_restart_output(struct term_action *a)
{
   *a = (struct term_action) {
      .type1 = a_restart_output,
      .arg = 0,
   };
}

static ALWAYS_INLINE void
term_make_action_set_cursor_enabled(struct term_action *a, bool value)
{
   *a = (struct term_action) {
      .type1 = a_enable_cursor,
      .arg = value,
   };
}

static ALWAYS_INLINE void
term_make_action_use_alt_buffer(struct term_action *a, bool value)
{
   *a = (struct term_action) {
      .type1 = a_use_alt_buffer,
      .arg = value,
   };
}

static ALWAYS_INLINE void
term_make_action_non_buf_scroll(struct term_action *a,
                                enum term_scroll_type st,
                                u32 rows)
{
   *a = (struct term_action) {
      .type2 = a_non_buf_scroll,
      .arg1 = rows,
      .arg2 = st,
   };
}

static ALWAYS_INLINE void
term_make_action_ins_blank_lines(struct term_action *a, u32 n)
{
   *a = (struct term_action) {
      .type1 = a_insert_blank_lines,
      .arg = n,
   };
}

static ALWAYS_INLINE void
term_make_action_del_lines(struct term_action *a, u32 n)
{
   *a = (struct term_action) {
      .type1 = a_delete_lines,
      .arg = n,
   };
}

static ALWAYS_INLINE void
term_make_action_set_scroll_region(struct term_action *a,
                                   u32 start,
                                   u32 end)
{
   *a = (struct term_action) {
      .type2 = a_set_scroll_region,
      .arg1 = start,
      .arg2 = end,
   };
}

static ALWAYS_INLINE void
term_make_action_ins_blank_chars(struct term_action *a, u16 num)
{
   *a = (struct term_action) {
      .type1 = a_insert_blank_chars,
      .arg = num,
   };
}

static ALWAYS_INLINE void
term_make_action_simple_del_chars(struct term_action *a, u16 num)
{
   *a = (struct term_action) {
      .type1 = a_simple_del_chars,
      .arg = num,
   };
}

static ALWAYS_INLINE void
term_make_action_simple_erase_chars(struct term_action *a, u16 num)
{
   *a = (struct term_action) {
      .type1 = a_simple_erase_chars,
      .arg = num,
   };
}

static void
term_execute_or_enqueue_action(struct vterm *t, struct term_action *a);
static void term_execute_action(struct vterm *t, struct term_action *a);
static void term_internal_non_buf_scroll_up(struct vterm *t, u16 n);
static void term_internal_non_buf_scroll_down(struct vterm *t, u16 n);
static void term_int_enable_cursor(struct vterm *t, bool val);
static void term_int_scroll_up(struct vterm *t, u32 lines);
static void term_int_scroll_down(struct vterm *t, u32 lines);
static void term_int_move_cur(struct vterm *t, int row, int col);
static void ts_buf_clear_row(struct vterm *t, u16 row, u8 color);
static void ts_clear_row(struct vterm *t, u16 row, u8 color);
static void term_int_scroll_up(struct vterm *t, u32 lines);
static void term_internal_write_char2(struct vterm *t, char c, u8 color);
static void term_internal_write_backspace(struct vterm *t, u8 color);
static inline void term_redraw(struct vterm *t);
static void term_redraw2(struct vterm *t, u16 s, u16 e);
static inline void term_redraw_scroll_region(struct vterm *t);
static void term_internal_delete_last_word(struct vterm *t, u8 color);
static int term_allocate_alt_buffers(struct vterm *t);
static void buf_copy_row(struct vterm *t, u32 dest, u32 src);
static ALWAYS_INLINE void ts_scroll_up(struct vterm *t, u32 lines);
static ALWAYS_INLINE void ts_scroll_down(struct vterm *t, u32 lines);
static ALWAYS_INLINE void ts_scroll_to_bottom(struct vterm *t);
static ALWAYS_INLINE u8 get_curr_cell_color(struct vterm *t);
static ALWAYS_INLINE u8 get_curr_cell_fg_color(struct vterm *t);

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

