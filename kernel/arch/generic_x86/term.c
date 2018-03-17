
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
#include <serial.h>

#define TERMINAL_VIDEO_ADDR ((volatile u16*) KERNEL_PA_TO_VA(0xB8000))
#define TERM_ROW_SIZE (term_width * 2)

static s8 term_width = 80;
static s8 term_height = 25;

void video_scroll_up(void)
{
   memmove((void *) TERMINAL_VIDEO_ADDR,
           (const void *) (TERMINAL_VIDEO_ADDR + term_width),
           TERM_ROW_SIZE * (term_height - 1));
}

void video_clear_row(int row_num)
{
   ASSERT(0 <= row_num && row_num < term_height);
   volatile u16 *row = TERMINAL_VIDEO_ADDR + term_width * row_num;
   bzero((void *)row, TERM_ROW_SIZE);
}

void video_set_char_at(char c, u8 color, int row, int col)
{
   ASSERT(0 <= row && row < term_height);
   ASSERT(0 <= col && col < term_width);

   volatile u16 *video = TERMINAL_VIDEO_ADDR;
   video[row * term_width + col] = make_vgaentry(c, color);
}

void video_movecur(int row, int col)
{
   u16 position = (row * term_width) + col;

   // cursor LOW port to vga INDEX register
   outb(0x3D4, 0x0F);
   outb(0x3D5, (unsigned char)(position & 0xFF));
   // cursor HIGH port to vga INDEX register
   outb(0x3D4, 0x0E);
   outb(0x3D5, (unsigned char)((position >> 8) & 0xFF));
}



#define TERMINAL_BUFFER_ROWS 1024
u16 term_buffer[TERMINAL_BUFFER_ROWS * 80];


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
          TERM_ROW_SIZE);
}

static void push_line_in_buffer(int videoRow)
{
   int destIndex = buf_next_slot % TERMINAL_BUFFER_ROWS;

   memcpy((void *)(term_buffer + destIndex * term_width),
          (const void *)(TERMINAL_VIDEO_ADDR + videoRow * term_width),
          TERM_ROW_SIZE);

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
   video_scroll_up();
   video_clear_row(term_height - 1);
}

void term_write_char_unsafe(char c)
{
   write_serial(c);

   if (scroll_value != 0) {
      term_scroll(0);
   }

   if (c == '\n') {
      terminal_column = 0;
      term_incr_row();
      video_movecur(terminal_row, terminal_column);
      return;
   }

   if (c == '\r') {
      terminal_column = 0;
      video_movecur(terminal_row, terminal_column);
      return;
   }

   if (c == '\t') {
      return;
   }

   video_set_char_at(c, terminal_color, terminal_row, terminal_column);
   ++terminal_column;

   if (terminal_column == term_width) {
      terminal_column = 0;
      term_incr_row();
   }

   video_movecur(terminal_row, terminal_column);
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

   video_movecur(row, col);
}

void term_init()
{
   video_movecur(0, 0);
   term_setcolor(make_color(COLOR_WHITE, COLOR_BLACK));

   for (int i = 0; i < term_height; i++)
      video_clear_row(i);

   init_serial_port();
}
