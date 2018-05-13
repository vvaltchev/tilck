
/*
 * This is a DEMO/DEBUG version of the tty device.
 *
 * Useful info:
 * http://www.linusakesson.net/programming/tty/index.php
 */

#include <exos/hal.h>
#include <exos/term.h>
#include <exos/serial.h>
#include <exos/interrupts.h>

static u8 term_width = 80;
static u8 term_height = 25;

static u8 terminal_row;
static u8 terminal_column;
static u8 terminal_color;

static const video_interface *vi;

void term_set_color(u8 color)
{
   terminal_color = color;
}

u8 term_get_color(void)
{
   return terminal_color;
}

void term_scroll_up(u32 lines)
{
   uptr var;
   disable_interrupts(&var);
   {
      vi->scroll_up(lines);

      if (!vi->is_at_bottom()) {
         vi->disable_cursor();
      } else {
         vi->enable_cursor();
         vi->move_cursor(terminal_row, terminal_column);
      }
   }
   enable_interrupts(&var);
}

void term_scroll_down(u32 lines)
{
   uptr var;
   disable_interrupts(&var);
   {
      vi->scroll_down(lines);

      if (vi->is_at_bottom()) {
         vi->enable_cursor();
         vi->move_cursor(terminal_row, terminal_column);
      }
   }
   enable_interrupts(&var);
}

static void term_incr_row()
{
   if (terminal_row < term_height - 1) {
      ++terminal_row;
      return;
   }

   vi->add_row_and_scroll();
}

static void term_write_char_unsafe(char c, u8 color)
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

      vi->set_char_at(' ', color, terminal_row, terminal_column);
      vi->move_cursor(terminal_row, terminal_column);
      return;
   }

   vi->set_char_at(c, color, terminal_row, terminal_column);
   ++terminal_column;

   if (terminal_column == term_width) {
      terminal_column = 0;
      term_incr_row();
   }

   vi->move_cursor(terminal_row, terminal_column);
}

void term_write_char(char c)
{
   uptr var;
   disable_interrupts(&var);
   {
      term_write_char_unsafe(c, terminal_color);
   }
   enable_interrupts(&var);
}

void term_write_char2(char c, u8 color)
{
   uptr var;
   disable_interrupts(&var);
   {
      term_write_char_unsafe(c, color);
   }
   enable_interrupts(&var);
}

void term_move_ch(int row, int col)
{
   uptr var;
   disable_interrupts(&var);
   {
      terminal_row = row;
      terminal_column = col;
      vi->move_cursor(row, col);
   }
   enable_interrupts(&var);
}

void init_term(const video_interface *interface, u8 default_color)
{
   ASSERT(!are_interrupts_enabled());

   vi = interface;

   vi->enable_cursor();
   term_move_ch(0, 0);
   term_set_color(default_color);

   for (int i = 0; i < term_height; i++)
      vi->clear_row(i);

   init_serial_port();
}

bool term_is_initialized(void)
{
   return vi != NULL;
}
