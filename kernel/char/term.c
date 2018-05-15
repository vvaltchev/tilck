
/*
 * This is a DEMO/DEBUG version of the tty device.
 *
 * Useful info:
 * http://www.linusakesson.net/programming/tty/index.php
 */

#include <common/arch/generic_x86/vga_textmode_defs.h>
#include <common/string_util.h>

#include <exos/term.h>
#include <exos/serial.h>
#include <exos/ringbuf.h>
#include <exos/kmalloc.h>
#include <exos/interrupts.h>

static u8 term_cols;
static u8 term_rows;

static u8 current_row;
static u8 current_col;
static u8 current_color;

static const video_interface *vi;

//////////////////////////


#define ROW_SIZE (term_cols * 2)
#define BUFFER_ROWS (term_rows * 1 + 9 * term_rows)
#define EXTRA_BUFFER_ROWS (BUFFER_ROWS - term_rows)

static u16 *scroll_buffer;
static u32 scroll;
static u32 max_scroll;

static bool ts_is_at_bottom(void)
{
   return scroll == max_scroll;
}


static void ts_set_scroll(u32 requested_scroll)
{
   /*
    * 1. scroll cannot be > max_scroll
    * 2. scroll cannot be < max_scroll - EXTRA_BUFFER_ROWS, where
    *    EXTRA_BUFFER_ROWS = BUFFER_ROWS - VIDEO_ROWS.
    *    In other words, if for example BUFFER_ROWS is 26, and max_scroll is
    *    1000, scroll cannot be less than 1000 + 25 - 26 = 999, which means
    *    exactly 1 scroll row (EXTRA_BUFFER_ROWS == 1).
    */

   const u32 min_scroll =
      max_scroll > EXTRA_BUFFER_ROWS
         ? max_scroll - EXTRA_BUFFER_ROWS
         : 0;

   requested_scroll = MIN(MAX(requested_scroll, min_scroll), max_scroll);

   if (requested_scroll == scroll)
      return; /* nothing to do */

   scroll = requested_scroll;

   for (u32 row = 0; row < term_rows; row++) {

      u32 buffer_row = (scroll + row) % BUFFER_ROWS;

      for (u32 col = 0; col < term_cols; col++) {
         u16 e = scroll_buffer[term_cols * buffer_row + col];
         vi->set_char_at(vgaentry_char(e), vgaentry_color(e), row, col);
      }
   }
}

static void ts_scroll_up(u32 lines)
{
   if (lines > scroll)
      ts_set_scroll(0);
   else
      ts_set_scroll(scroll - lines);
}

static void ts_scroll_down(u32 lines)
{
   ts_set_scroll(scroll + lines);
}

static void ts_scroll_to_bottom(void)
{
   if (scroll != max_scroll) {
      ts_set_scroll(max_scroll);
   }
}

static void ts_clear_row(int row_num, u8 color)
{
   u16 *rowb = scroll_buffer + term_cols * ((row_num + scroll) % BUFFER_ROWS);
   memset16(rowb, make_vgaentry(' ', color), term_cols);
   vi->clear_row(row_num, color);
}

static void ts_add_row_and_scroll(u8 color)
{
   max_scroll++;
   ts_set_scroll(max_scroll);
   ts_clear_row(term_rows - 1, color);
}

////////////////////////////////////////////////////////////////////

/* ---------------- term actions --------------------- */

static void term_action_set_color(u8 color)
{
   current_color = color;
}

static void term_action_scroll_up(u32 lines)
{
   ts_scroll_up(lines);

   if (!ts_is_at_bottom()) {
      vi->disable_cursor();
   } else {
      vi->enable_cursor();
      vi->move_cursor(current_row, current_col);
   }
}

static void term_action_scroll_down(u32 lines)
{
   ts_scroll_down(lines);

   if (ts_is_at_bottom()) {
      vi->enable_cursor();
      vi->move_cursor(current_row, current_col);
   }
}

static void term_incr_row()
{
   if (current_row < term_rows - 1) {
      ++current_row;
      return;
   }

   ts_add_row_and_scroll(current_color);
}

static void term_action_write_char2(char c, u8 color)
{
   write_serial(c);
   ts_scroll_to_bottom();
   vi->enable_cursor();

   if (c == '\n') {
      current_col = 0;
      term_incr_row();
      vi->move_cursor(current_row, current_col);
      return;
   }

   if (c == '\r') {
      current_col = 0;
      vi->move_cursor(current_row, current_col);
      return;
   }

   if (c == '\t') {
      return;
   }

   if (c == '\b') {
      if (current_col > 0) {
         current_col--;
      }

      scroll_buffer[(current_row + scroll) % BUFFER_ROWS * term_cols + current_col] = make_vgaentry(' ', color);
      vi->set_char_at(' ', color, current_row, current_col);
      vi->move_cursor(current_row, current_col);
      return;
   }

   scroll_buffer[(current_row + scroll) % BUFFER_ROWS * term_cols + current_col] = make_vgaentry(c, color);
   vi->set_char_at(c, color, current_row, current_col);
   ++current_col;

   if (current_col == term_cols) {
      current_col = 0;
      term_incr_row();
   }

   vi->move_cursor(current_row, current_col);
}

static void term_action_move_ch_and_cur(int row, int col)
{
   current_row = row;
   current_col = col;
   vi->move_cursor(row, col);
}

/* ---------------- term action engine --------------------- */

typedef enum {

   a_write_char2      = 0,
   a_move_ch_and_cur  = 1,
   a_scroll_up        = 2,
   a_scroll_down      = 3,
   a_set_color        = 4

} term_action_type;

typedef struct {

   union {

      struct {
         u32 type2 :  8;
         u32 arg1  : 12;
         u32 arg2  : 12;
      };

      struct {
         u32 type1 :  8;
         u32 arg   : 24;
      };

      u32 raw;
   };

} term_action;

typedef void (*action_func)();

typedef struct {

   action_func func;
   u32 args_count;

} actions_table_item;

static actions_table_item actions_table[] = {

   [a_write_char2] = {(action_func)term_action_write_char2, 2},
   [a_move_ch_and_cur] = {(action_func)term_action_move_ch_and_cur, 2},
   [a_scroll_up] = {(action_func)term_action_scroll_up, 1},
   [a_scroll_down] = {(action_func)term_action_scroll_down, 1},
   [a_set_color] = {(action_func)term_action_set_color, 1}
};

static void term_execute_action(term_action a)
{
   actions_table_item *it = &actions_table[a.type2];

   switch (it->args_count) {
      case 2:
         it->func(a.arg1, a.arg2);
         break;
      case 1:
         it->func(a.arg);
         break;
      default:
         NOT_REACHED();
      break;
   }
}

static ringbuf term_ringbuf;
static term_action term_actions_buf[32];

void term_execute_or_enqueue_action(term_action a)
{
   bool written;
   bool was_empty;

   written = ringbuf_write_elem_ex(&term_ringbuf, &a, &was_empty);

   /*
    * written would be false only if the ringbuf was full. In order that to
    * happen, we'll need ARRAY_SIZE(term_actions_buf) nested interrupts and
    * all of them need to issue a term_* call. Virtually "impossible".
    */
   VERIFY(written);

   if (was_empty) {

      while (ringbuf_read_elem(&term_ringbuf, &a))
         term_execute_action(a);

   }
}

/* ---------------- term interface --------------------- */


void term_write_char2(char c, u8 color)
{
   term_action a = {
      .type2 = a_write_char2,
      .arg1 = c,
      .arg2 = color
   };

   term_execute_or_enqueue_action(a);
}

void term_move_ch_and_cur(int row, int col)
{
   term_action a = {
      .type2 = a_move_ch_and_cur,
      .arg1 = row,
      .arg2 = col
   };

   term_execute_or_enqueue_action(a);
}

void term_scroll_up(u32 lines)
{
   term_action a = {
      .type1 = a_scroll_up,
      .arg = lines
   };

   term_execute_or_enqueue_action(a);
}

void term_scroll_down(u32 lines)
{
   term_action a = {
      .type1 = a_scroll_down,
      .arg = lines
   };

   term_execute_or_enqueue_action(a);
}

void term_set_color(u8 color)
{
   term_action a = {
      .type1 = a_set_color,
      .arg = color
   };

   term_execute_or_enqueue_action(a);
}

void term_write_char(char c)
{
   term_write_char2(c, current_color);
}

/* ---------------- term non-action interface funcs --------------------- */

bool term_is_initialized(void)
{
   return vi != NULL;
}

void
init_term(const video_interface *intf, int rows, int cols, u8 default_color)
{
   ASSERT(!are_interrupts_enabled());

   vi = intf;
   term_cols = cols;
   term_rows = rows;

   ringbuf_init(&term_ringbuf,
                ARRAY_SIZE(term_actions_buf),
                sizeof(term_action),
                term_actions_buf);

   scroll_buffer = kmalloc(2 * BUFFER_ROWS * term_cols);
   VERIFY(scroll_buffer != NULL); // We cannot handle this.

   vi->enable_cursor();
   term_action_move_ch_and_cur(0, 0);
   term_action_set_color(default_color);

   for (int i = 0; i < term_rows; i++)
      ts_clear_row(i, default_color);

   init_serial_port();
}
