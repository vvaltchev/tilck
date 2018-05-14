
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
#include <exos/ringbuf.h>

static u8 term_width = 80;
static u8 term_height = 25;

static u8 terminal_row;
static u8 terminal_column;
static u8 terminal_color;

static const video_interface *vi;

/* ---------------- term actions --------------------- */

static void term_action_set_color(u8 color)
{
   terminal_color = color;
}

static void term_action_scroll_up(u32 lines)
{
   vi->scroll_up(lines);

   if (!vi->is_at_bottom()) {
      vi->disable_cursor();
   } else {
      vi->enable_cursor();
      vi->move_cursor(terminal_row, terminal_column);
   }
}

static void term_action_scroll_down(u32 lines)
{
   vi->scroll_down(lines);

   if (vi->is_at_bottom()) {
      vi->enable_cursor();
      vi->move_cursor(terminal_row, terminal_column);
   }
}

static void term_incr_row()
{
   if (terminal_row < term_height - 1) {
      ++terminal_row;
      return;
   }

   vi->add_row_and_scroll();
}

static void term_action_write_char2(char c, u8 color)
{
   write_serial(c);
   vi->scroll_to_bottom();
   vi->enable_cursor();

/* temp debug stuff */
   if (c == '~') {
      for (int i = 0; i < 100*1000*1000; i++) { }
   }
/* end temp debug stuff */

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

static void term_action_move_ch(int row, int col)
{
   terminal_row = row;
   terminal_column = col;
   vi->move_cursor(row, col);
}

/* ---------------- term action engine --------------------- */

typedef enum {

   a_write_char2  = 0,
   a_move_ch      = 1,
   a_scroll_up    = 2,
   a_scroll_down  = 3,
   a_set_color    = 4

} term_action_type;

typedef struct {

   union {

      struct {
         u32 type2 : 8;
         u32 arg1 : 12;
         u32 arg2 : 12;
      };

      struct {
         u32 type1 : 8;
         u32 arg : 24;
      };

      u32 raw;
   };

} term_action;

typedef void (*action_func)();
typedef void (*action_func1)(uptr);
typedef void (*action_func2)(uptr, uptr);

typedef struct {

   action_func f;
   u32 args_count;

} actions_table_item;

static actions_table_item actions_table[] = {

   [a_write_char2] = {(action_func)term_action_write_char2, 2},
   [a_move_ch] = {(action_func)term_action_move_ch, 2},
   [a_scroll_up] = {(action_func)term_action_scroll_up, 1},
   [a_scroll_down] = {(action_func)term_action_scroll_down, 1},
   [a_set_color] = {(action_func)term_action_set_color, 1}
};

static void term_execute_action(term_action a)
{
   actions_table_item *it = &actions_table[a.type2];

   if (it->args_count == 2) {
      action_func2 f = (action_func2) it->f;
      f(a.arg1, a.arg2);
   } else if (it->args_count == 1) {
      action_func1 f = (action_func1) it->f;
      f(a.arg);
   } else {
      NOT_REACHED();
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

void term_move_ch(int row, int col)
{
   term_action a = {
      .type2 = a_move_ch,
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
   term_write_char2(c, terminal_color);
}

/* ---------------- term non-action interface funcs --------------------- */

bool term_is_initialized(void)
{
   return vi != NULL;
}

void init_term(const video_interface *interface, u8 default_color)
{
   ASSERT(!are_interrupts_enabled());

   vi = interface;

   ringbuf_init(&term_ringbuf,
                ARRAY_SIZE(term_actions_buf),
                sizeof(term_action),
                term_actions_buf);

   vi->enable_cursor();
   term_action_move_ch(0, 0);
   term_action_set_color(default_color);

   for (int i = 0; i < term_height; i++)
      vi->clear_row(i);

   init_serial_port();
}
