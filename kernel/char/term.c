
/*
 * This is a DEMO/DEBUG version of the tty device.
 *
 * Useful info:
 * http://www.linusakesson.net/programming/tty/index.php
 */

#include <hal.h>
#include <term.h>
#include <serial.h>

static u8 term_width = 80;
static u8 term_height = 25;

static u8 terminal_row;
static u8 terminal_column;
static u8 terminal_color;

static const video_interface *vi;

void term_scroll_up(u32 lines)
{
   disable_interrupts();
   {
      vi->scroll_up(lines);

      if (!vi->is_at_bottom()) {
         vi->disable_cursor();
      } else {
         vi->enable_cursor();
         vi->move_cursor(terminal_row, terminal_column);
      }
   }
   enable_interrupts();
}

void term_scroll_down(u32 lines)
{
   disable_interrupts();
   {
      vi->scroll_down(lines);

      if (vi->is_at_bottom()) {
         vi->enable_cursor();
         vi->move_cursor(terminal_row, terminal_column);
      }
   }
   enable_interrupts();
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

   vi->add_row_and_scroll();
}

void term_write_char_unsafe(char c)
{
   write_serial(c);
   vi->scroll_to_bottom();
   vi->enable_cursor();

   if (c == '\n') {
      terminal_column = 0;
      term_incr_row();
      vi->move_cursor(terminal_row, terminal_column);
      return;
   }

   if (c == '\r') {
      terminal_column = 0;
      vi->move_cursor(terminal_row, terminal_column);
      return;
   }

   if (c == '\t') {
      return;
   }

   if (c == '\b') {
      if (terminal_column > 0) {
         terminal_column--;
      }

      vi->set_char_at(' ', terminal_color, terminal_row, terminal_column);
      vi->move_cursor(terminal_row, terminal_column);
      return;
   }

   vi->set_char_at(c, terminal_color, terminal_row, terminal_column);
   ++terminal_column;

   if (terminal_column == term_width) {
      terminal_column = 0;
      term_incr_row();
   }

   vi->move_cursor(terminal_row, terminal_column);
}

void term_write_char(char c)
{
   disable_interrupts();
   {
      term_write_char_unsafe(c);
   }
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
   disable_interrupts();
   {
      terminal_row = row;
      terminal_column = col;
      vi->move_cursor(row, col);
   }
}

void term_init(const video_interface *interface, u8 default_color)
{
   ASSERT(!are_interrupts_enabled());

   vi = interface;

   vi->enable_cursor();
   term_move_ch(0, 0);
   term_setcolor(default_color);

   for (int i = 0; i < term_height; i++)
      vi->clear_row(i);

   init_serial_port();
}
