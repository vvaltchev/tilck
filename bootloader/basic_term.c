#include <common/basic_defs.h>
#include <common/string_util.h>
#include <common/arch/generic_x86/x86_utils.h>
#include <common/arch/generic_x86/vga_textmode_defs.h>

#define TERMINAL_VIDEO_ADDR ((u16*)(0xB8000))

#define TERM_WIDTH  80
#define TERM_HEIGHT 25

uint8_t terminal_row = 0;
uint8_t terminal_column = 0;
uint8_t terminal_color = 0;

void term_setcolor(uint8_t color) {
   terminal_color = color;
}

void term_movecur(int row, int col)
{
   uint16_t position = (row * 80) + col;

   // cursor LOW port to vga INDEX register
   outb(0x3D4, 0x0F);
   outb(0x3D5, (unsigned char)(position & 0xFF));
   // cursor HIGH port to vga INDEX register
   outb(0x3D4, 0x0E);
   outb(0x3D5, (unsigned char)((position >> 8) & 0xFF));
}


static void term_incr_row()
{
   if (terminal_row < TERM_HEIGHT - 1) {
      ++terminal_row;
      return;
   }

   // We have to scroll...

   memmove(TERMINAL_VIDEO_ADDR,
           TERMINAL_VIDEO_ADDR + 2 * TERM_WIDTH,
           TERM_WIDTH * TERM_HEIGHT * 2);

   volatile uint16_t *lastRow =
      (volatile uint16_t *)TERMINAL_VIDEO_ADDR + TERM_WIDTH * (TERM_HEIGHT - 1);

   for (int i = 0; i < TERM_WIDTH; i++) {
      lastRow[i] = make_vgaentry(' ', terminal_color);
   }
}

void term_write_char(char c) {

   if (c == '\n') {
      terminal_column = 0;
      term_incr_row();
      term_movecur(terminal_row, terminal_column);
      return;
   }

   if (c == '\r') {
      terminal_column = 0;
      term_movecur(terminal_row, terminal_column);
      return;
   }

   if (c == '\t') {
      return;
   }

   volatile uint16_t *video = (volatile uint16_t *)TERMINAL_VIDEO_ADDR;

   const size_t offset = terminal_row * TERM_WIDTH + terminal_column;
   video[offset] = make_vgaentry(c, terminal_color);
   ++terminal_column;

   if (terminal_column == TERM_WIDTH) {
      terminal_column = 0;
      term_incr_row();
   }

   term_movecur(terminal_row, terminal_column);
}

void term_write_string(const char *str)
{
   while (*str) {
      term_write_char(*str++);
   }
}

void init_term() {

   term_movecur(0, 0);

   term_setcolor(make_color(COLOR_WHITE, COLOR_BLACK));
   volatile uint16_t *ptr = (volatile uint16_t *)TERMINAL_VIDEO_ADDR;

   for (int i = 0; i < TERM_WIDTH*TERM_HEIGHT; ++i) {
      *ptr++ = make_vgaentry(' ', terminal_color);
   }
}
