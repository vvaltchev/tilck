/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

enum term_scroll_type {
   term_scroll_up = 0,
   term_scroll_down = 1,
};

enum term_action_type {

   a_none,
   a_write,
   a_dwrite_no_filter,           // [1]
   a_del_generic,
   a_scroll,                     // [2]
   a_set_col_offset,
   a_move_ch_and_cur,
   a_move_ch_and_cur_rel,
   a_reset,
   a_pause_video_output,
   a_restart_video_output,
   a_enable_cursor,
   a_use_alt_buffer,
   a_non_buf_scroll,             // [3]
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

typedef void (*action_func)(struct term *t, ...);

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
         u32 type3 :  4;
         u32 len   : 20;
         u32 col   :  8;
      };

      struct {
         u32 type2 :  4;
         u32 arg1  : 14;
         u32 arg2  : 14;
      };

      struct {
         u32 type1  :  4;
         u32 arg    : 28;
      };

   };

   ulong ptr;
};

STATIC_ASSERT(sizeof(struct term_action) == (2 * sizeof(ulong)));

u16 term_get_curr_row(struct term *t);
u16 term_get_curr_col(struct term *t);

static ALWAYS_INLINE void
term_make_action_write(struct term_action *a,
                       const char *buf,
                       size_t len,
                       u8 color)
{
   *a = (struct term_action) {
      .type3 = a_write,
      .len = UNSAFE_MIN((u32)len, (u32)MB - 1),
      .col = color,
      .ptr = (ulong)buf,
   };
}

static ALWAYS_INLINE void
term_make_action_direct_write(struct term_action *a,
                              const char *buf,
                              size_t len,
                              u8 color)
{
   *a = (struct term_action) {
      .type3 = a_dwrite_no_filter,
      .len = UNSAFE_MIN((u32)len, (u32)MB - 1),
      .col = color,
      .ptr = (ulong)buf,
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
      .type2 = a_move_ch_and_cur,
      .arg1 = row,
      .arg2 = col,
   };
}

static ALWAYS_INLINE void
term_make_action_move_cursor_rel(struct term_action *a, int dr, int dc)
{
   *a = (struct term_action) {
      .type2 = a_move_ch_and_cur_rel,
      .arg1 = LO_BITS((u32)dr, 8, u32),
      .arg2 = LO_BITS((u32)dc, 8, u32),
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
term_make_action_pause_video_output(struct term_action *a)
{
   *a = (struct term_action) {
      .type1 = a_pause_video_output,
      .arg = 0,
   };
}

static ALWAYS_INLINE void
term_make_action_restart_video_output(struct term_action *a)
{
   *a = (struct term_action) {
      .type1 = a_restart_video_output,
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
