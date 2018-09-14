/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/fs/devfs.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/ringbuf.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/kb.h>

#include <termios.h>      // system header

#include "term_int.h"
#include "tty_int.h"

#define KB_INPUT_BS 4096

extern struct termios c_term;
extern struct termios default_termios;

static char kb_input_buf[KB_INPUT_BS];
static ringbuf kb_input_ringbuf;
static kcond kb_input_cond;

static void tty_keypress_echo(char c)
{
   if (c == '\n' && (c_term.c_lflag & ECHONL)) {
      /*
       * From termios' man page:
       *
       *    ECHONL: If ICANON is also set, echo the NL character even if ECHO
       *            is not set.
       */
      term_write(&c, 1, tty_curr_color);
      return;
   }

   if (!(c_term.c_lflag & ECHO)) {
      /* If ECHO is not enabled, just don't echo. */
      return;
   }

   /* echo is enabled */

   if (c_term.c_lflag & ICANON) {

      if (c == c_term.c_cc[VEOF]) {
         /* In canonical mode, EOF is never echoed */
         return;
      }

      if (c_term.c_lflag & ECHOK) {
         if (c == c_term.c_cc[VKILL]) {
            term_write(TERM_KILL_S, 1, tty_curr_color);
            return;
         }
      }

      if (c_term.c_lflag & ECHOE) {

        /*
         * From termios' man page:
         *
         *    ECHOE
         *        If ICANON is also set, the ERASE character erases the
         *        preceding input character, and WERASE erases the preceding
         *        word.
         */


         if (c == c_term.c_cc[VWERASE]) {
            term_write(TERM_WERASE_S, 1, tty_curr_color);
            return;
         }

         if (c == c_term.c_cc[VERASE]) {
            term_write(TERM_ERASE_S, 1, tty_curr_color);
            return;
         }
      }
   }

   /*
    * From termios' man page:
    *
    * ECHOCTL
    *          (not  in  POSIX)  If  ECHO is also set, terminal special
    *          characters other than TAB, NL, START, and STOP are echoed as ^X,
    *          where X is the character with ASCII code 0x40 greater than the
    *          special character.  For  example, character 0x08 (BS) is echoed
    *          as ^H.
    *
    */
   if ((c < ' ' || c == 0x7F) && (c_term.c_lflag & ECHOCTL)) {
      if (c != '\t' && c != '\n') {
         if (c != c_term.c_cc[VSTART] && c != c_term.c_cc[VSTOP]) {
            c += 0x40;
            term_write("^", 1, tty_curr_color);
            term_write(&c, 1, tty_curr_color);
            return;
         }
      }
   }

   if (c == '\a' || c == '\f' || c == '\v') {
      /* ignore some characters */
      return;
   }

   /* Just ECHO a regular character */
   term_write(&c, 1, tty_curr_color);
}

static inline bool kb_buf_is_empty(void)
{
   return ringbuf_is_empty(&kb_input_ringbuf);
}

static inline char kb_buf_read_elem(void)
{
   u8 ret;
   ASSERT(!kb_buf_is_empty());
   DEBUG_CHECKED_SUCCESS(ringbuf_read_elem1(&kb_input_ringbuf, &ret));
   return (char)ret;
}

static inline bool kb_buf_drop_last_written_elem(void)
{
   char unused;
   tty_keypress_echo(c_term.c_cc[VERASE]);
   return ringbuf_unwrite_elem(&kb_input_ringbuf, &unused);
}

static inline bool kb_buf_write_elem(char c)
{
   tty_keypress_echo(c);
   return ringbuf_write_elem1(&kb_input_ringbuf, c);
}

static int tty_handle_non_printable_key(u32 key)
{
   char seq[16];
   bool found = kb_scancode_to_ansi_seq(key, kb_get_current_modifiers(), seq);
   const char *p = seq;

   if (!found) {
      /* Unknown/unsupported sequence: just do nothing avoiding weird effects */
      return KB_HANDLER_NAK;
   }

   while (*p) {
      kb_buf_write_elem(*p++);
   }

   if (!(c_term.c_lflag & ICANON))
      kcond_signal_one(&kb_input_cond);

   return KB_HANDLER_OK_AND_CONTINUE;
}

static volatile int tty_end_line_delim_count = 0;
#include "tty_ctrl_handlers.c.h"

static inline bool tty_is_line_delim_char(char c)
{
   return c == '\n' ||
          c == c_term.c_cc[VEOF] ||
          c == c_term.c_cc[VEOL] ||
          c == c_term.c_cc[VEOL2];
}

static int tty_keypress_handle_canon_mode(u32 key, u8 c)
{
   if (c == c_term.c_cc[VERASE]) {

      kb_buf_drop_last_written_elem();

   } else {

      kb_buf_write_elem(c);

      if (tty_is_line_delim_char(c)) {
         tty_end_line_delim_count++;
         kcond_signal_one(&kb_input_cond);
      }
   }

   return KB_HANDLER_OK_AND_CONTINUE;
}

int tty_keypress_handler_int(u32 key, u8 c, bool check_mods)
{
   if (!c)
      return tty_handle_non_printable_key(key);

   if (check_mods && kb_is_alt_pressed())
      kb_buf_write_elem('\033');

   if (check_mods && kb_is_ctrl_pressed()) {
      if (isalpha(c) || c == '\\') {
         /* ctrl ignores the case of the letter */
         c = toupper(c) - 'A' + 1;
      }
   }

   if (c == '\r') {

      if (c_term.c_iflag & IGNCR)
         return KB_HANDLER_OK_AND_CONTINUE; /* ignore the carriage return */

      if (c_term.c_iflag & ICRNL)
         c = '\n';

   } else if (c == '\n') {

      if (c_term.c_iflag & INLCR)
         c = '\r';
   }

   if (check_mods && tty_handle_special_controls(c)) /* Ctrl+C, Ctrl+D etc. */
      return KB_HANDLER_OK_AND_CONTINUE;

   if (c_term.c_lflag & ICANON)
      return tty_keypress_handle_canon_mode(key, c);

   /* raw mode input handling */
   kb_buf_write_elem(c);

   kcond_signal_one(&kb_input_cond);
   return KB_HANDLER_OK_AND_CONTINUE;
}

int tty_keypress_handler(u32 key, u8 c)
{
   if (key == KEY_PAGE_UP && kb_is_shift_pressed()) {
      term_scroll_up(5);
      return KB_HANDLER_OK_AND_STOP;
   }

   if (key == KEY_PAGE_DOWN && kb_is_shift_pressed()) {
      term_scroll_down(5);
      return KB_HANDLER_OK_AND_STOP;
   }

   return tty_keypress_handler_int(key, c, true);
}

static u32 tty_flush_read_buf(devfs_file_handle *h, char *buf, u32 size)
{
   u32 rem = h->read_buf_used - h->read_pos;
   u32 m = MIN(rem, size);
   memcpy(buf, h->read_buf + h->read_pos, m);
   h->read_pos += m;

   if (h->read_pos == h->read_buf_used) {
      h->read_buf_used = 0;
      h->read_pos = 0;
   }

   return m;
}

/*
 * Returns:
 *    - TRUE when caller's read loop should continue
 *    - FALSE when caller's read loop should STOP
 */
static bool
tty_internal_read_single_char_from_kb(devfs_file_handle *h,
                                      bool *delim_break)
{
   char c = kb_buf_read_elem();
   h->read_buf[h->read_buf_used++] = c;

   if (c_term.c_lflag & ICANON) {

      if (tty_is_line_delim_char(c)) {
         ASSERT(tty_end_line_delim_count > 0);
         tty_end_line_delim_count--;
         *delim_break = true;

         /* All line delimiters except EOF are kept */
         if (c == c_term.c_cc[VEOF])
            h->read_buf_used--;
      }

      return !*delim_break;
   }

   /*
    * In raw mode it makes no sense to read until a line delim is
    * found: we should read the minimum necessary.
    */
   return !(h->read_buf_used >= c_term.c_cc[VMIN]);
}

static inline bool
tty_internal_should_read_return(devfs_file_handle *h,
                                u32 read_cnt,
                                bool delim_break)
{
   if (c_term.c_lflag & ICANON) {
      return
         delim_break ||
            (tty_end_line_delim_count > 0 &&
               (h->read_buf_used == DEVFS_READ_BS || read_cnt == KB_INPUT_BS));
   }

   /* Raw mode handling */
   return read_cnt >= c_term.c_cc[VMIN];
}

ssize_t tty_read(fs_handle fsh, char *buf, size_t size)
{
   devfs_file_handle *h = fsh;
   size_t read_count = 0;
   bool delim_break;

   ASSERT(is_preemption_enabled());

   if (!size)
      return 0;

   if (h->read_buf_used)
      return tty_flush_read_buf(h, buf, size);

   if (c_term.c_lflag & ICANON)
      term_set_col_offset(term_get_curr_col());

   do {

      while (kb_buf_is_empty()) {
         kcond_wait(&kb_input_cond, NULL, KCOND_WAIT_FOREVER);
      }

      delim_break = false;

      ASSERT(h->read_buf_used == 0);
      ASSERT(h->read_pos == 0);

      while (!kb_buf_is_empty() &&
             h->read_buf_used < DEVFS_READ_BS &&
             tty_internal_read_single_char_from_kb(h, &delim_break)) { }

      read_count += tty_flush_read_buf(h, buf + read_count, size - read_count);
      ASSERT(tty_end_line_delim_count >= 0);

   } while (!tty_internal_should_read_return(h, read_count, delim_break));

   return read_count;
}

void tty_input_init(void)
{
   kcond_init(&kb_input_cond);
   ringbuf_init(&kb_input_ringbuf, KB_INPUT_BS, 1, kb_input_buf);
   tty_update_special_ctrl_handlers();

   if (kb_register_keypress_handler(&tty_keypress_handler) < 0)
      panic("TTY: unable to register keypress handler");
}
