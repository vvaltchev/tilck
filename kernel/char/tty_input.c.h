
// NOTE: on Linux this buffer is 4K, but for exOS 256 seems enough.
#define KB_INPUT_BUF_SIZE 256

extern struct termios c_term;
extern struct termios default_termios;

static char kb_input_buf[KB_INPUT_BUF_SIZE];
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
      term_write(&c, 1);
      return;
   }

   if (!(c_term.c_lflag & ECHO)) {
      /* If ECHO is not enabled, just don't echo. */
      return;
   }

   /* echo is enabled */

   if (c == c_term.c_cc[VERASE]) {
      /*
       * From termios' man page:
       *    If ICANON is also set, the ERASE character erases the preceding
       *    input character, and WERASE erases the preceding word.
       */
      if ((c_term.c_lflag & ICANON) && (c_term.c_lflag & ECHOE)) {
         term_write("\b", 1);
         return;
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
            term_write("^", 1);
            term_write(&c, 1);
            return;
         }
      }
   }

   if (c == '\a' || c == '\f') {
      /* ignore the bell and form feed characters */
      return;
   }

   /* Just ECHO a regular character */
   term_write(&c, 1);
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

static int tty_keypress_handle_canon_mode(u32 key, u8 c)
{
   if (c == c_term.c_cc[VERASE]) {

      kb_buf_drop_last_written_elem();

   } else {

      kb_buf_write_elem(c);

      if (c == '\n')
         kcond_signal_one(&kb_input_cond);
   }

   return KB_HANDLER_OK_AND_CONTINUE;
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

#include "tty_ctrl_handlers.c.h"

static int tty_keypress_handler(u32 key, u8 c)
{
   if (key == KEY_PAGE_UP && kb_is_shift_pressed()) {
      term_scroll_up(5);
      return KB_HANDLER_OK_AND_STOP;
   }

   if (key == KEY_PAGE_DOWN && kb_is_shift_pressed()) {
      term_scroll_down(5);
      return KB_HANDLER_OK_AND_STOP;
   }

   if (!c)
      return tty_handle_non_printable_key(key);

   if (kb_is_alt_pressed())
      kb_buf_write_elem('\033');

   if (kb_is_ctrl_pressed()) {
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

   if (tty_handle_special_controls(c))
      return KB_HANDLER_OK_AND_CONTINUE;

   if (c_term.c_lflag & ICANON)
      return tty_keypress_handle_canon_mode(key, c);

   /* raw mode input handling */
   kb_buf_write_elem(c);

   kcond_signal_one(&kb_input_cond);
   return KB_HANDLER_OK_AND_CONTINUE;
}

static ssize_t tty_read(fs_handle h, char *buf, size_t size)
{
   size_t read_count = 0;
   ASSERT(is_preemption_enabled());

   if (!size)
      return read_count;

   if (c_term.c_lflag & ICANON)
      term_set_col_offset(term_get_curr_col());

   while (true) {

      while (kb_buf_is_empty()) {
         kcond_wait(&kb_input_cond, NULL, KCOND_WAIT_FOREVER);
      }

      while (read_count < size && !kb_buf_is_empty()) {
         buf[read_count++] = kb_buf_read_elem();
      }

      if (c_term.c_lflag & ICANON) {

         if (read_count == KB_INPUT_BUF_SIZE)
            break;

         char lc = buf[read_count - 1];

         if (lc == '\n')
            break;

         if (lc == c_term.c_cc[VEOF]) {
            read_count--;
            break;
         }

      } else {

         if (read_count > 0)
            break;
      }

   }

   return read_count;
}
