
#pragma once
#include <common_defs.h>

void term_init();
void term_setcolor(u8 color);
void term_movecur(int row, int col);
void term_write_char(char c);
void term_write_string(const char *str);
void term_move_ch(int row, int col);
void term_scroll_up(u32 lines);
void term_scroll_down(u32 lines);
