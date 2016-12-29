
#pragma once

#include <common_defs.h>


/* Hardware text mode color constants. */
enum vga_color {
   COLOR_BLACK = 0,
   COLOR_BLUE = 1,
   COLOR_GREEN = 2,
   COLOR_CYAN = 3,
   COLOR_RED = 4,
   COLOR_MAGENTA = 5,
   COLOR_BROWN = 6,
   COLOR_LIGHT_GREY = 7,
   COLOR_DARK_GREY = 8,
   COLOR_LIGHT_BLUE = 9,
   COLOR_LIGHT_GREEN = 10,
   COLOR_LIGHT_CYAN = 11,
   COLOR_LIGHT_RED = 12,
   COLOR_LIGHT_MAGENTA = 13,
   COLOR_LIGHT_BROWN = 14,
   COLOR_WHITE = 15,
};

static inline u8 make_color(enum vga_color fg, enum vga_color bg) {
   return fg | bg << 4;
}

static inline u16 make_vgaentry(char c, u8 color) {
   u16 c16 = c;
   u16 color16 = color;
   return c16 | color16 << 8;
}


void term_init();
void term_setcolor(u8 color);
void term_movecur(int row, int col);
void term_write_char(char c);
void term_write_string(const char *str);
void term_move_ch(int row, int col);
void term_scroll(int lines);
int term_get_scroll_value();
