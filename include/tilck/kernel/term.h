/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

struct term;
struct term_action;

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

struct term *alloc_term_struct(void);
void free_term_struct(struct term *t);
void dispose_term(struct term *t);
void set_curr_term(struct term *t);

enum term_fret {
   TERM_FILTER_WRITE_BLANK,
   TERM_FILTER_WRITE_C,
};

typedef enum term_fret (*term_filter)(u8 *c,                 /* in/out */
                                      u8 *color,             /* in/out */
                                      struct term_action *a, /*  out   */
                                      void *ctx);            /*   in   */

void term_set_filter(struct term *t, term_filter func, void *ctx);

static ALWAYS_INLINE struct term *get_curr_term(void) {
   return __curr_term;
}

/* --- debug funcs --- */
void debug_term_dump_font_table(struct term *t);
