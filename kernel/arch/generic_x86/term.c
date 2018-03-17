
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

int video_get_scroll(void);
int video_get_max_scroll(void);
void video_set_scroll(int scroll);
void video_scroll_to_bottom(void);
void video_add_row_and_scroll(void);
void video_clear_row(int row_num);
void video_set_char_at(char c, u8 color, int row, int col);
void video_movecur(int row, int col);
void video_enable_cursor(void);
void video_disable_cursor(void);

u8 term_width = 80;
u8 term_height = 25;

u8 terminal_row;
u8 terminal_column;
u8 terminal_color;

int term_get_scroll_value()
{
   return video_get_scroll();
}

void term_scroll(int s)
{
   video_set_scroll(s);

   if (s < video_get_max_scroll()) {
      video_disable_cursor();
   } else {
      video_enable_cursor();
      video_movecur(terminal_row, terminal_column);
   }
}

void term_setcolor(u8 color) {
   terminal_color = color;
}

// static void increase_buf_next_slot(int val)
// {
//    if (val < 0) {
//       buf_next_slot += val;

//       if (buf_next_slot < 0)
//          buf_next_slot += TERMINAL_BUFFER_ROWS;
//       return;
//    }

//    if (buf_next_slot + val >= TERMINAL_BUFFER_ROWS) {  // we'll wrap around
//       buf_full = true;
//    }

//    buf_next_slot = (buf_next_slot + val) % TERMINAL_BUFFER_ROWS;
// }

// static void from_buffer_to_video(int bufRow, int videoRow)
// {
//    if (bufRow < 0) {
//       bufRow += TERMINAL_BUFFER_ROWS;
//    } else {
//       bufRow %= TERMINAL_BUFFER_ROWS;
//    }

//    memcpy((void *)(TERMINAL_VIDEO_ADDR + videoRow * term_width),
//           (const void *)(term_buffer + bufRow * term_width),
//           TERM_ROW_SIZE);
// }

// static void push_line_in_buffer(int videoRow)
// {
//    int destIndex = buf_next_slot % TERMINAL_BUFFER_ROWS;

//    memcpy((void *)(term_buffer + destIndex * term_width),
//           (const void *)(TERMINAL_VIDEO_ADDR + videoRow * term_width),
//           TERM_ROW_SIZE);

//    increase_buf_next_slot(1);
// }

// static void pop_line_from_buffer(int videoRow)
// {
//    ASSERT(buf_next_slot > 0);

//    from_buffer_to_video(buf_next_slot - 1, videoRow);
//    increase_buf_next_slot(-1);
// }


// void term_scroll(int lines)
// {
//    int max_scroll_lines = 0;

//    if (lines < 0) {
//       lines = 0;
//    }

//    if (lines == 0) {

//       if (scroll_value == 0) {
//          return;
//       }

//       // just restore the video buffer

//       for (int i = 0; i < term_height; i++) {
//          pop_line_from_buffer(term_height - i - 1);
//       }

//       scroll_value = 0;
//       return;
//    }


//    max_scroll_lines = buf_full
//                       ? TERMINAL_BUFFER_ROWS
//                       : MIN(buf_next_slot, TERMINAL_BUFFER_ROWS);

//    if (scroll_value == 0) {

//       // if the current scroll_value is 0,
//       // save the whole current screen buffer.

//       for (int i = 0; i < term_height; i++) {
//          push_line_in_buffer(i);
//       }

//    } else {

//       max_scroll_lines -= term_height;
//    }

//    lines = MIN(lines, max_scroll_lines);

//    for (int i = 0; i < term_height; i++) {

//       from_buffer_to_video(buf_next_slot - 1 - lines - i,
//                            term_height - i - 1);
//    }

//    scroll_value = lines;
// }


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

   if (c == '\b') {
      if (terminal_column > 0) {
         terminal_column--;
      }

      video_set_char_at(' ', terminal_color, terminal_row, terminal_column);
      video_movecur(terminal_row, terminal_column);
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
