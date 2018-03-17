
/*
 * This is a DEMO/DEBUG version of the tty driver.
 * Until the concept of character devices is implemented in exOS, that's
 * good enough for basic experiments.
 *
 * Useful info:
 * http://www.linusakesson.net/programming/tty/index.php
 */

#include <term.h>
#include <string_util.h>
#include <paging.h>
#include <arch/generic_x86/x86_utils.h>

#define TERMINAL_VIDEO_ADDR ((volatile u16*) KERNEL_PA_TO_VA(0xB8000))

#define TERMINAL_BUFFER_ROWS 1024
u16 term_buffer[TERMINAL_BUFFER_ROWS * 80];

#define TERMINAL_SCREEN_SIZE (term_width * term_height * 2)

static s8 term_width = 80;
static s8 term_height = 25;

u8 terminal_row;
u8 terminal_column;
u8 terminal_color;

int buf_next_slot;
int scroll_value;
bool buf_full;

int term_get_scroll_value()
{
   return scroll_value;
}

void term_setcolor(u8 color) {
   terminal_color = color;
}

void term_movecur(int row, int col)
{
   u16 position = (row * term_width) + col;

   // cursor LOW port to vga INDEX register
   outb(0x3D4, 0x0F);
   outb(0x3D5, (unsigned char)(position & 0xFF));
   // cursor HIGH port to vga INDEX register
   outb(0x3D4, 0x0E);
   outb(0x3D5, (unsigned char)((position >> 8) & 0xFF));
}


static void increase_buf_next_slot(int val)
{
   if (val < 0) {
      buf_next_slot += val;

      if (buf_next_slot < 0)
         buf_next_slot += TERMINAL_BUFFER_ROWS;
      return;
   }

   if (buf_next_slot + val >= TERMINAL_BUFFER_ROWS) {  // we'll wrap around
      buf_full = true;
   }

   buf_next_slot = (buf_next_slot + val) % TERMINAL_BUFFER_ROWS;
}

static void from_buffer_to_video(int bufRow, int videoRow)
{
   if (bufRow < 0) {
      bufRow += TERMINAL_BUFFER_ROWS;
   } else {
      bufRow %= TERMINAL_BUFFER_ROWS;
   }

   memcpy((void *)(TERMINAL_VIDEO_ADDR + videoRow * term_width),
          (const void *)(term_buffer + bufRow * term_width),
          term_width * 2);
}

static void push_line_in_buffer(int videoRow)
{
   int destIndex = buf_next_slot % TERMINAL_BUFFER_ROWS;

   memcpy((void *)(term_buffer + destIndex * term_width),
          (const void *)(TERMINAL_VIDEO_ADDR + videoRow * term_width),
          term_width * 2);

   increase_buf_next_slot(1);
}

static void pop_line_from_buffer(int videoRow)
{
   ASSERT(buf_next_slot > 0);

   from_buffer_to_video(buf_next_slot - 1, videoRow);
   increase_buf_next_slot(-1);
}


void term_scroll(int lines)
{
   int max_scroll_lines = 0;

   if (lines < 0) {
      lines = 0;
   }

   if (lines == 0) {

      if (scroll_value == 0) {
         return;
      }

      // just restore the video buffer

      for (int i = 0; i < term_height; i++) {
         pop_line_from_buffer(term_height - i - 1);
      }

      scroll_value = 0;
      return;
   }


   max_scroll_lines = buf_full
                      ? TERMINAL_BUFFER_ROWS
                      : MIN(buf_next_slot, TERMINAL_BUFFER_ROWS);

   if (scroll_value == 0) {

      // if the current scroll_value is 0,
      // save the whole current screen buffer.

      for (int i = 0; i < term_height; i++) {
         push_line_in_buffer(i);
      }

   } else {

      max_scroll_lines -= term_height;
   }

   lines = MIN(lines, max_scroll_lines);

   for (int i = 0; i < term_height; i++) {

      from_buffer_to_video(buf_next_slot - 1 - lines - i,
                           term_height - i - 1);
   }

   scroll_value = lines;
}

static void term_incr_row()
{
   if (terminal_row < term_height - 1) {
      ++terminal_row;
      return;
   }

   push_line_in_buffer(0);

   // We have to scroll...

   memmove((void *) TERMINAL_VIDEO_ADDR,
           (const void *) (TERMINAL_VIDEO_ADDR + term_width),
           term_width * (term_height - 1) * 2);

   volatile u16 *lastRow =
      TERMINAL_VIDEO_ADDR + term_width * (term_height - 1);

   for (int i = 0; i < term_width; i++) {
      lastRow[i] = make_vgaentry(' ', terminal_color);
   }
}

///////////////////////////

#define COM1 0x3f8

void init_serial_port()
{
   outb(COM1 + 1, 0x00);    // Disable all interrupts
   outb(COM1 + 3, 0x80);    // Enable DLAB (set baud rate divisor)
   outb(COM1 + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
   outb(COM1 + 1, 0x00);    //                  (hi byte)
   outb(COM1 + 3, 0x03);    // 8 bits, no parity, one stop bit
   outb(COM1 + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
   outb(COM1 + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

int serial_received() {
   return inb(COM1 + 5) & 1;
}

char read_serial() {
   while (serial_received() == 0);
   return inb(COM1);
}

int is_transmit_empty() {
   return inb(COM1 + 5) & 0x20;
}

void write_serial(char a) {
   while (is_transmit_empty() == 0);
   outb(COM1, a);
}

//////////////////////////

void term_write_char_unsafe(char c)
{
   write_serial(c);

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

   volatile u16 *video = TERMINAL_VIDEO_ADDR;

   if (c == '\b') {

      if (terminal_column > 0) {
         --terminal_column;
      }

      const size_t offset = terminal_row * term_width + terminal_column;
      video[offset] = make_vgaentry(' ', terminal_color);

      term_movecur(terminal_row, terminal_column);
      return;
   }

   const size_t offset = terminal_row * term_width + terminal_column;
   video[offset] = make_vgaentry(c, terminal_color);
   ++terminal_column;

   if (terminal_column == term_width) {
      terminal_column = 0;
      term_incr_row();
   }

   term_movecur(terminal_row, terminal_column);
}

void term_write_char(char c)
{
   disable_interrupts();
   term_write_char_unsafe(c);
   enable_interrupts();
}

void term_write_string(const char *str)
{
   disable_interrupts();

   while (*str) {
      term_write_char_unsafe(*str++);
   }

   enable_interrupts();
}

void term_move_ch(int row, int col)
{
   terminal_row = row;
   terminal_column = col;

   term_movecur(row, col);
}

void term_init() {

   u8 defColor = make_color(COLOR_WHITE, COLOR_BLACK);
   term_movecur(0, 0);

   volatile u16 *ptr = TERMINAL_VIDEO_ADDR;

   for (int i = 0; i < term_width*term_height; ++i) {
      *ptr++ = make_vgaentry(' ', defColor);
   }

   term_setcolor(defColor);
   init_serial_port();
}
