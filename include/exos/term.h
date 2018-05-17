
#pragma once
#include <common/basic_defs.h>

typedef struct {

   /* Main functions */
   void (*set_char_at)(int row, int col, u16 entry);
   void (*set_row)(int row, u16 *data);
   void (*clear_row)(int row_num, u8 color);

   /* Cursor management */
   void (*move_cursor)(int row, int col);
   void (*enable_cursor)(void);
   void (*disable_cursor)(void);

   /* Other (optional) */
   void (*scroll_one_line_up)(void);

} video_interface;


void init_term(const video_interface *vi, int rows, int cols, u8 default_color);
bool term_is_initialized(void);

void term_write(char *buf, u32 len);
void term_write2(char *buf, u32 len, u8 color);
void term_write_char(char c);
void term_write_char2(char c, u8 color);
void term_move_ch_and_cur(int row, int col);
void term_scroll_up(u32 lines);
void term_scroll_down(u32 lines);
void term_set_color(u8 color);
