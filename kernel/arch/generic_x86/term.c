
/*
 * This is a DEMO/DEBUG version of the tty driver.
 *
 * Useful info:
 * http://www.linusakesson.net/programming/tty/index.php
 */

#include <term.h>
#include <arch/generic_x86/textmode_video.h>
#include <serial.h>

u8 term_width = 80;
u8 term_height = 25;

u8 terminal_row;
u8 terminal_column;
u8 terminal_color;

void term_scroll_up(u32 lines)
{
   video_scroll_up(lines);

   if (!video_is_at_bottom()) {
      video_disable_cursor();
   } else {
      video_enable_cursor();
      video_move_cursor(terminal_row, terminal_column);
   }
}

void term_scroll_down(u32 lines)
{
   video_scroll_down(lines);

   if (video_is_at_bottom()) {
      video_enable_cursor();
      video_move_cursor(terminal_row, terminal_column);
   }
}

void term_setcolor(u8 color) {
   terminal_color = color;
}

static void term_incr_row()
{
   if (terminal_row < term_height - 1) {
      ++terminal_row;
      return;
   }

   video_add_row_and_scroll();
}

void term_write_char_unsafe(char c)
{
   write_serial(c);
   video_scroll_to_bottom();
   video_enable_cursor();

   if (c == '\n') {
      terminal_column = 0;
      term_incr_row();
      video_move_cursor(terminal_row, terminal_column);
      return;
   }

   if (c == '\r') {
      terminal_column = 0;
      video_move_cursor(terminal_row, terminal_column);
      return;
   }

   if (c == '\t') {
      return;
   }

   if (c == '\b') {
      if (terminal_column > 0) {
         terminal_column--;
      }

      video_set_char_at(' ', terminal_color, terminal_row, terminal_column);
      video_move_cursor(terminal_row, terminal_column);
      return;
   }

   video_set_char_at(c, terminal_color, terminal_row, terminal_column);
   ++terminal_column;

   if (terminal_column == term_width) {
      terminal_column = 0;
      term_incr_row();
   }

   video_move_cursor(terminal_row, terminal_column);
}

void term_write_char(char c)
{
   disable_interrupts();
   term_write_char_unsafe(c);
   enable_interrupts();
}

void term_write_string(const char *str)
{
   /*
    * NOTE: This function uses intentionally the 'safe' version of
    * term_write_char() in order to keep the interrupts-disabled periods
    * shorter.
    */
   while (*str) {
      term_write_char(*str++);
   }
}

void term_move_ch(int row, int col)
{
   terminal_row = row;
   terminal_column = col;
   video_move_cursor(row, col);
}

void term_init()
{
   term_move_ch(0, 0);
   term_setcolor(make_color(COLOR_WHITE, COLOR_BLACK));

   for (int i = 0; i < term_height; i++)
      video_clear_row(i);

   init_serial_port();
}
