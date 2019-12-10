/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

struct term;
struct term_action;
struct term_interface;

extern struct term *__curr_term;
extern const struct term_interface *__curr_term_intf;
extern const struct term_interface *video_term_intf;
extern const struct term_interface *serial_term_intf;

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

struct term_params {

   u16 rows;
   u16 cols;
   const struct video_interface *vi;
};

enum term_type {
   term_type_video,
   term_type_serial,
};

enum term_fret {
   TERM_FILTER_WRITE_BLANK,
   TERM_FILTER_WRITE_C,
};

typedef enum term_fret (*term_filter)(u8 *c,                 /* in/out */
                                      u8 *color,             /* in/out */
                                      struct term_action *a, /*  out   */
                                      void *ctx);            /*   in   */

struct term_interface {

   enum term_type (*get_type)(void);
   bool (*is_initialized)(struct term *t);
   void (*get_params)(struct term *t, struct term_params *out);

   void (*write)(struct term *t, const char *buf, size_t len, u8 color);
   void (*scroll_up)(struct term *t, u32 lines);
   void (*scroll_down)(struct term *t, u32 lines);
   void (*set_col_offset)(struct term *t, int off);
   void (*pause_video_output)(struct term *t);
   void (*restart_video_output)(struct term *t);
   void (*set_filter)(struct term *t, term_filter func, void *ctx);

   /*
    * The first term must be pre-allocated but _not_ pre-initialized.
    * It is expected to require init() to be called on it before use.
    */
   struct term *(*get_first_term)(void);

   int (*video_term_init)(struct term *t,
                          const struct video_interface *vi,
                          u16 rows,
                          u16 cols,
                          int rows_buf); /* note: < 0 means default value */

   int (*serial_term_init)(struct term *t,
                           u16 serial_port_fwd);

   struct term *(*alloc)(void);
   void (*free)(struct term *t);
   void (*dispose)(struct term *t);

   /* --- debug funcs --- */
   void (*debug_dump_font_table)(struct term *t);
};

void set_curr_video_term(struct term *t);
void register_term_intf(const struct term_interface *intf);

void
init_first_video_term(const struct video_interface *vi,
                      u16 rows,
                      u16 cols,
                      int rows_buf);

void init_first_serial_term(u16 port);
void init_first_term_null(void);

static ALWAYS_INLINE struct term *get_curr_term(void) {
   return __curr_term;
}

static ALWAYS_INLINE const struct term_interface *get_curr_term_intf(void) {
   return __curr_term_intf;
}

static ALWAYS_INLINE bool term_is_initialized(void)
{
   if (!__curr_term_intf)
      return false;

   return __curr_term_intf->is_initialized(__curr_term);
}

static ALWAYS_INLINE void term_read_info(struct term_params *out)
{
   __curr_term_intf->get_params(__curr_term, out);
}

static ALWAYS_INLINE void term_write(const char *buf, size_t len, u8 color)
{
   __curr_term_intf->write(__curr_term, buf, len, color);
}

static ALWAYS_INLINE void term_pause_video_output(void)
{
   __curr_term_intf->pause_video_output(__curr_term);
}

static ALWAYS_INLINE void term_restart_video_output(void)
{
   __curr_term_intf->restart_video_output(__curr_term);
}
