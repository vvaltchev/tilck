
#pragma once
#include <common/basic_defs.h>

typedef struct {

   /* Main functions */
   void (*set_char_at)(char c, u8 color, int row, int col);
   void (*clear_row)(int row_num);

   /* Scrolling */
   void (*scroll_up)(u32 lines);
   void (*scroll_down)(u32 lines);
   bool (*is_at_bottom)(void);
   void (*scroll_to_bottom)(void);
   void (*add_row_and_scroll)(void);

   /* Cursor management */
   void (*move_cursor)(int row, int col);
   void (*enable_cursor)(void);
   void (*disable_cursor)(void);

} video_interface;


void term_init(const video_interface *vi, u8 default_color);
void term_setcolor(u8 color);
void term_movecur(int row, int col);
void term_write_char(char c);
void term_write_string(const char *str);
void term_move_ch(int row, int col);
void term_scroll_up(u32 lines);
void term_scroll_down(u32 lines);
