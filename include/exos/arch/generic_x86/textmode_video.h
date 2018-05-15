
#pragma once
#include <common/basic_defs.h>
#include <common/arch/generic_x86/vga_textmode_defs.h>

/* Main functions */
void textmode_set_char_at(char c, u8 color, int row, int col);
void textmode_clear_row(int row_num, u8 color);

/* Scrolling */
void textmode_scroll_up(u32 lines);
void textmode_scroll_down(u32 lines);
bool textmode_is_at_bottom(void);
void textmode_scroll_to_bottom(void);
void textmode_add_row_and_scroll(u8 color);

/* Cursor management */
void textmode_move_cursor(int row, int col);
void textmode_enable_cursor(void);
void textmode_disable_cursor(void);
