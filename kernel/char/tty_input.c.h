
// NOTE: on Linux this buffer is 4K, but for exOS 256 seems enough.
#define KB_INPUT_BUF_SIZE 256

extern struct termios c_term;
extern struct termios default_termios;

static char kb_input_buf[KB_INPUT_BUF_SIZE];
static ringbuf kb_input_ringbuf;
static kcond kb_input_cond;

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
   return ringbuf_unwrite_elem(&kb_input_ringbuf, &unused);
}

static inline bool kb_buf_write_elem(char c)
{
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

      kb_buf_write_elem(*p);
      tty_keypress_echo(*p);

      p++;
   }

   if (!(c_term.c_lflag & ICANON))
      kcond_signal_one(&kb_input_cond);

   return KB_HANDLER_OK_AND_CONTINUE;
}

static void tty_handle_ctrl_plus_letter(u8 c)
{
   ASSERT(isalpha(c));
   char letter = toupper(c); /* ctrl ignores the case of the letter */
   char t = letter - 'A' + 1;

   if (t == '\r') {
      if (c_term.c_iflag & ICRNL)
         t = '\n';
   } else if (t == c_term.c_cc[VSTOP]) {
      if (c_term.c_iflag & IXON) {
         // printk("Ctrl + S (pause transmission) not supported\n");
         // TODO: eventually support pause transmission, one day.
      }
   } else if (t == c_term.c_cc[VSTART]) {
      if (c_term.c_iflag & IXON) {
         // printk("Ctrl + Q (resume transmission) not supported\n");
         // TODO: eventually support resume transmission, one day.
      }
   } else if (t == c_term.c_cc[VINTR]) {
      // TODO: handle Ctrl + C according to ISIG when signals are supported
   } else if (t == c_term.c_cc[VSUSP]) {
      // TODO: handle Ctrl + Z according to ISIG when signals are supported
   }

   kb_buf_write_elem(t);
   tty_keypress_echo(t);
}

/*
 * The user pressed a printable key, but with modifiers (shift, alt, ctrl)
 */
static int tty_handle_pchar_with_mods(u8 c)
{
   if (!isalpha(c)) {
       /* Ignore ctrl/alt + <non-letter> sequences for the moment. */
      return KB_HANDLER_NAK;
   }

   if (kb_is_alt_pressed()) {
      kb_buf_write_elem('\033');
      tty_keypress_echo('\033');
   }

   if (kb_is_ctrl_pressed()) {
      tty_handle_ctrl_plus_letter(c);
   } else {
      kb_buf_write_elem(c);
      tty_keypress_echo(c);
   }

   if (!(c_term.c_lflag & ICANON))
      kcond_signal_one(&kb_input_cond);

   return KB_HANDLER_OK_AND_CONTINUE;
}

static int tty_keypress_handler(u32 key, u8 c)
{
   if (key == KEY_PAGE_UP) {
      if (kb_is_pressed(KEY_L_SHIFT) || kb_is_pressed(KEY_R_SHIFT)) {
         term_scroll_up(5);
         return KB_HANDLER_OK_AND_STOP;
      }
   }

   if (key == KEY_PAGE_DOWN) {
      if (kb_is_pressed(KEY_L_SHIFT) || kb_is_pressed(KEY_R_SHIFT)) {
         term_scroll_down(5);
         return KB_HANDLER_OK_AND_STOP;
      }
   }

   if (!c)
      return tty_handle_non_printable_key(key);

   if (kb_get_current_modifiers() > 2)
      return tty_handle_pchar_with_mods(c);

   tty_keypress_echo(c);

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

   do {

      while (kb_buf_is_empty()) {
         kcond_wait(&kb_input_cond, NULL, KCOND_WAIT_FOREVER);
      }

      while (read_count < size && !kb_buf_is_empty()) {
         buf[read_count++] = kb_buf_read_elem();
      }

      if (read_count > 0 && !(c_term.c_lflag & ICANON))
         break;

   } while (buf[read_count - 1] != '\n' || read_count == KB_INPUT_BUF_SIZE);

   return read_count;
}
