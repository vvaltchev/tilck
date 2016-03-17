
#include <term.h>
#include <stringUtil.h>

#define TERMINAL_VIDEO_ADDR ((char *)0xB8000)
#define TERMINAL_BUFFER_ADDR ((char *)0x200000)

#define TERMINAL_BUFFER_ROWS 1000

static const size_t TERM_WIDTH = 80;
static const size_t TERM_HEIGHT = 25;

volatile uint8_t terminal_row;
volatile uint8_t terminal_column;
volatile uint8_t terminal_color;

volatile uint16_t buffer_rows_used;
volatile uint16_t scroll_value;


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

void term_init() {

   terminal_row = 0;
   terminal_column = 0;
   buffer_rows_used = 0;
   scroll_value = 0;

   term_movecur(0, 0);

   term_setcolor(make_color(COLOR_WHITE, COLOR_BLACK));
   volatile uint16_t *ptr = (volatile uint16_t *)TERMINAL_VIDEO_ADDR;

   for (int i = 0; i < TERM_WIDTH*TERM_HEIGHT; ++i) {
      *ptr++ = make_vgaentry(' ', terminal_color);
   }
}

static void term_incr_row()
{
   if (terminal_row < TERM_HEIGHT - 1) {

      ++terminal_row;
      return;
   }

   // We have to scroll...

   memcpy(TERMINAL_VIDEO_ADDR,
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

   volatile uint16_t *video = (volatile uint16_t *)TERMINAL_VIDEO_ADDR;

   if (c == '\b') {

      if (terminal_column > 0) {
         --terminal_column;
      }

      const size_t offset = terminal_row * TERM_WIDTH + terminal_column;
      video[offset] = make_vgaentry(' ', terminal_color);

      term_movecur(terminal_row, terminal_column);
      return;
   }

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

void term_move_ch(int row, int col)
{
   terminal_row = row;
   terminal_column = col;

   term_movecur(row, col);
}

void show_hello_message()
{
   term_write_string("Hello from my kernel!\n");
}
