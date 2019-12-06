/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

struct term;
extern struct term *__curr_term;

struct video_interface {

   /* Main functions */
   void (*set_char_at)(u16 row, u16 col, u16 entry);
   void (*set_row)(u16 row, u16 *data, bool flush); // NOTE: set_row() can
                                                    // safely assume that it has
                                                    // been called in a FPU
                                                    // context.
   void (*clear_row)(u16 row_num, u8 color);

   /* Cursor management */
   void (*move_cursor)(u16 row, u16 col, int color);
   void (*enable_cursor)(void);
   void (*disable_cursor)(void);

   /* Other (optional) */
   void (*scroll_one_line_up)(void);
   void (*flush_buffers)(void);
   void (*redraw_static_elements)(void);
   void (*disable_static_elems_refresh)(void);
   void (*enable_static_elems_refresh)(void);
};

struct tilck_term_info {

   u16 tab_size;
   u16 rows;
   u16 cols;

   const struct video_interface *vi;
};

int init_term(struct term *t,
              const struct video_interface *vi,
              u16 rows,
              u16 cols,
              u16 serial_port_fwd,
              int rows_buf); /* note: < 0 means default value */

bool term_is_initialized(struct term *t);
void term_read_info(struct term *t, struct tilck_term_info *out);

void term_write(struct term *t, const char *buf, size_t len, u8 color);
void term_scroll_up(struct term *t, u32 lines);
void term_scroll_down(struct term *t, u32 lines);
void term_set_col_offset(struct term *t, u32 off);
void term_pause_video_output(struct term *t);
void term_restart_video_output(struct term *t);
void term_set_cursor_enabled(struct term *t, bool value);

/* --- debug funcs --- */
void debug_term_dump_font_table(struct term *t);

#define CHAR_BLOCK_LIGHT  0xb0  //  #
#define CHAR_BLOCK_MID    0xb1  //  #
#define CHAR_BLOCK_HEAVY  0xdb  //  #
#define CHAR_VLINE        0xb3  //   |
#define CHAR_RTEE         0xb4  //  -|
#define CHAR_LTEE         0xc3  //  |-
#define CHAR_LLCORNER     0xc0  //  |_
#define CHAR_LRCORNER     0xd9  //  _|
#define CHAR_ULCORNER     0xda  //
#define CHAR_URCORNER     0xbf  //
#define CHAR_BTEE         0xc1  //  _|_
#define CHAR_TTEE         0xc2  //   T
#define CHAR_HLINE        0xc4  //  --
#define CHAR_CROSS        0xc5  //  +
#define CHAR_DIAMOND      0x04
#define CHAR_DEGREE       0xf8
#define CHAR_PLMINUS      0xf1
#define CHAR_BULLET       0x07
#define CHAR_LARROW       0x1b
#define CHAR_RARROW       0x1a
#define CHAR_DARROW       0x19
#define CHAR_UARROW       0x18

static ALWAYS_INLINE struct term *get_curr_term(void) {
   return __curr_term;
}
