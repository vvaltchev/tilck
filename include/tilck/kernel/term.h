/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

#define TERM_ERASE_C   '\b'
#define TERM_ERASE_S   "\b"

#define TERM_WERASE_C  0x17    /* typical value for TERM=linux, Ctrl + W */
#define TERM_WERASE_S  "\x17"

#define TERM_KILL_C    0x15    /* typical value for TERM=linux, Ctrl + 7 */
#define TERM_KILL_S    "\x15"

typedef struct {

   /* Main functions */
   void (*set_char_at)(int row, int col, u16 entry);
   void (*set_row)(int row, u16 *data, bool flush); // NOTE: set_row() can
                                                    // safely assume that it has
                                                    // been called in a FPU
                                                    // context.
   void (*clear_row)(int row_num, u8 color);

   /* Cursor management */
   void (*move_cursor)(int row, int col, int color);
   void (*enable_cursor)(void);
   void (*disable_cursor)(void);

   /* Other (optional) */
   void (*scroll_one_line_up)(void);
   void (*flush_buffers)(void);
   void (*redraw_static_elements)(void);
   void (*disable_static_elems_refresh)(void);
   void (*enable_static_elems_refresh)(void);

} video_interface;


void init_term(const video_interface *vi, int rows, int cols);
bool term_is_initialized(void);

u32 term_get_tab_size(void);
u32 term_get_rows(void);
u32 term_get_cols(void);

u32 term_get_curr_row(void);
u32 term_get_curr_col(void);

void term_write(const char *buf, u32 len, u8 color);
void term_scroll_up(u32 lines);
void term_scroll_down(u32 lines);
void term_set_col_offset(u32 off);
void term_move_ch_and_cur(u32 row, u32 col);
void term_move_ch_and_cur_rel(s8 dx, s8 dy);
void term_pause_video_output(void);
void term_restart_video_output(void);

/* --- term write filter interface --- */

enum term_fret {
   TERM_FILTER_WRITE_BLANK,
   TERM_FILTER_WRITE_C
};

typedef struct {

   union {

      struct {
         u64 type3 :  4;
         u64 len   : 20;
         u64 col   :  8;
         u64 ptr   : 32;
      };

      struct {
         u64 type2 :  4;
         u64 arg1  : 30;
         u64 arg2  : 30;
      };

      struct {
         u64 type1  :  4;
         u64 arg    : 32;
         u64 unused : 28;
      };

      u64 raw;
   };

} term_action;

typedef enum term_fret (*term_filter_func)(u8 c,
                                           u8 *color /* in/out */,
                                           term_action *a /* out */,
                                           void *ctx);

void term_set_filter_func(term_filter_func func, void *ctx);
term_filter_func term_get_filter_func(void);

/* --- debug funcs --- */
void debug_term_dump_font_table(void);

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

/* Other functions */

void init_console(void); /* generic console init: fb or text mode */
