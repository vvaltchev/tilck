
#include <term.h>
#include <stringUtil.h>

#define TERMINAL_VIDEO_ADDR ((char*)0xB8000)
#define TERMINAL_BUFFER_ADDR ((char*)0x10000)

#define TERMINAL_BUFFER_ROWS 28
#define TERMINAL_SCREEN_SIZE (TERM_WIDTH * TERM_HEIGHT * 2)

static int8_t TERM_WIDTH = 80;
static int8_t TERM_HEIGHT = 25;

volatile uint8_t terminal_row = 0;
volatile uint8_t terminal_column = 0;
volatile uint8_t terminal_color;

volatile int buf_next_slot = 0;
volatile int scroll_value = 0;
volatile bool buf_full = false;

int term_get_scroll_value()
{
   return scroll_value;
}

void term_setcolor(uint8_t color) {
   terminal_color = color;
}


void term_movecur(int row, int col)
{
   uint16_t position = (row * TERM_WIDTH) + col;

   // cursor LOW port to vga INDEX register
   outb(0x3D4, 0x0F);
   outb(0x3D5, (unsigned char)(position & 0xFF));
   // cursor HIGH port to vga INDEX register
   outb(0x3D4, 0x0E);
   outb(0x3D5, (unsigned char)((position >> 8) & 0xFF));
}

void term_init() {

   term_movecur(0, 0);
   term_setcolor(make_color(COLOR_WHITE, COLOR_BLACK));

   volatile uint16_t *ptr = (volatile uint16_t *)TERMINAL_VIDEO_ADDR;

   for (int i = 0; i < TERM_WIDTH*TERM_HEIGHT; ++i) {
      *ptr++ = make_vgaentry(' ', terminal_color);
   }
}

static void put_line_in_buffer(int lineIndex)
{
   volatile char *video = (volatile char *)TERMINAL_VIDEO_ADDR;
   volatile char *buf = (volatile char *)TERMINAL_BUFFER_ADDR;

   int destIndex = buf_next_slot; // % TERMINAL_BUFFER_ROWS;

   memcpy(buf + destIndex * 2 * TERM_WIDTH,
          video + lineIndex * 2 * TERM_WIDTH, 2 * TERM_WIDTH);

   //buf_next_slot = (buf_next_slot + 1) % TERMINAL_BUFFER_ROWS;
   buf_next_slot++;

   if (buf_next_slot == 0) {  // we wrapped-around
      //buf_full = true;
   }
}

static void from_buffer_to_video(int bufRow, int videoRow)
{
   volatile char *video = (volatile char *)TERMINAL_VIDEO_ADDR;
   volatile char *buf = (volatile char *)TERMINAL_BUFFER_ADDR;

   bufRow %= TERMINAL_BUFFER_ROWS;

   memcpy(video + videoRow * 2 * TERM_WIDTH,
          buf + bufRow * 2 * TERM_WIDTH, 2 * TERM_WIDTH);
}

void term_scroll(int lines)
{
   if (lines < 0) {
      return;
   }

   if (lines == 0) {

      // just restore the video buffer
      for (int i = 0; i < TERM_HEIGHT; i++) {

         from_buffer_to_video(buf_next_slot - lines - 1 - i, // backwards read
                              TERM_HEIGHT - i - 1);          // backwards write
      }

      buf_next_slot -= TERM_HEIGHT;
      scroll_value = 0;
      return;
   }

   // if the current scroll_value is 0,
   // save the whole current screen buffer.

   if (scroll_value == 0) {

      if (buf_full) {
         lines = MIN(lines, TERMINAL_BUFFER_ROWS);
      } else {
         lines = MIN(lines, MIN(buf_next_slot, TERMINAL_BUFFER_ROWS));
      }

      for (int i = 0; i < TERM_HEIGHT; i++) {
         put_line_in_buffer(i);
      }

   } else {

      if (buf_full) {
         lines = MIN(lines, TERMINAL_BUFFER_ROWS - TERM_HEIGHT);
      } else {
         lines = MIN(lines, MIN(buf_next_slot, TERMINAL_BUFFER_ROWS) - TERM_HEIGHT);
      }
   }

   for (int i = 0; i < TERM_HEIGHT; i++) {

      from_buffer_to_video(buf_next_slot - 1 - lines - i,
                           TERM_HEIGHT - i - 1);
   }

   scroll_value = lines;
}

static void term_incr_row()
{
   if (terminal_row < TERM_HEIGHT - 1) {
      ++terminal_row;
      return;
   }

   put_line_in_buffer(0);

   // We have to scroll...

   memmove(TERMINAL_VIDEO_ADDR,
           TERMINAL_VIDEO_ADDR + 2 * TERM_WIDTH,
           TERM_WIDTH * (TERM_HEIGHT - 1) * 2);

   volatile uint16_t *lastRow =
      (volatile uint16_t *)TERMINAL_VIDEO_ADDR + TERM_WIDTH * (TERM_HEIGHT - 1);

   for (int i = 0; i < TERM_WIDTH; i++) {
      lastRow[i] = make_vgaentry(' ', terminal_color);
   }
}

void term_write_char(char c) {

   if (scroll_value != 0) {
      term_scroll(0);
   }

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

