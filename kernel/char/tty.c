
#include <exos/common/basic_defs.h>
#include <exos/common/string_util.h>

#include <exos/kernel/fs/exvfs.h>
#include <exos/kernel/fs/devfs.h>
#include <exos/kernel/errno.h>
#include <exos/kernel/kmalloc.h>
#include <exos/kernel/sync.h>
#include <exos/kernel/kb.h>
#include <exos/kernel/process.h>
#include <exos/kernel/term.h>
#include <exos/kernel/user.h>
#include <exos/kernel/ringbuf.h>
#include <exos/kernel/kb_scancode_set1_keys.h>

#include <termios.h>      // system header

extern struct termios curr_termios;
extern const struct termios default_termios;

#define KB_INPUT_BUF_SIZE 256

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
   if (c == '\b') {

      kb_buf_drop_last_written_elem();

   } else {

      kb_buf_write_elem(c);

      if (c == '\n')
         kcond_signal_one(&kb_input_cond);
   }

   return KB_HANDLER_OK_AND_CONTINUE;
}

static const struct {

   u32 key;
   char seq[8];

} ansi_sequences[] = {

   {KEY_UP,           "\x1b[A"},
   {KEY_DOWN,         "\x1b[B"},
   {KEY_RIGHT,        "\x1b[C"},
   {KEY_LEFT,         "\x1b[D"},

   {KEY_NUMPAD_UP,    "\x1b[A"},
   {KEY_NUMPAD_DOWN,  "\x1b[B"},
   {KEY_NUMPAD_RIGHT, "\x1b[C"},
   {KEY_NUMPAD_LEFT,  "\x1b[D"},

   {KEY_PAGE_UP,      "\x01b[5~"},
   {KEY_PAGE_DOWN,    "\x01b[6~"},

   {KEY_INS,          "\x01b[2~"},
   {KEY_DEL,          "\x01b[3~"},
   {KEY_HOME,         "\x01b[H"},
   {KEY_END,          "\x01b[F"},
};

static const char *kb_scancode_to_ansi_seq(u32 key)
{
   for (u32 i = 0; i < ARRAY_SIZE(ansi_sequences); i++)
      if (ansi_sequences[i].key == key)
         return ansi_sequences[i].seq;

   return NULL;
}

static int
tty_term_write_filter(char *c,
                      u8 *color,
                      term_int_write_char_func write_char_func,
                      void *ctx)
{
   switch (*c) {

      case '\033':
         write_char_func('^', *color);
         write_char_func('[', *color);
         return TERM_FILTER_FUNC_RET_BLANK;

      case '\000':
         write_char_func('^', *color);
         write_char_func('@', *color);
         return TERM_FILTER_FUNC_RET_BLANK;

      case '\a':
         /* ignore the bell character */
         return TERM_FILTER_FUNC_RET_BLANK;

      case '\f':
         /* ignore the form feed character, for the moment */
         return TERM_FILTER_FUNC_RET_BLANK;

      case '\t':
      case '\b':
      case '\v':
      case '\r':
      case '\n':
      return TERM_FILTER_FUNC_RET_WRITE_C;
   }

   if (1 <= *c && *c <= 26) {
      char letter = *c + 'A' - 1;
      write_char_func('^', *color);
      write_char_func(letter, *color);
      return TERM_FILTER_FUNC_RET_BLANK;
   }

   return TERM_FILTER_FUNC_RET_WRITE_C;
}

static int tty_handle_non_printable_key(u32 key)
{
   const char *seq = kb_scancode_to_ansi_seq(key);
   const char *p = seq;

   if (!seq) {
      /* Unknown/unsupported sequence: just do nothing avoiding weird effects */
      return KB_HANDLER_NAK;
   }

   while (*p) {

      kb_buf_write_elem(*p);

      if (curr_termios.c_lflag & ECHO)
         term_write(p, 1);

      p++;
   }

   if (!(curr_termios.c_lflag & ICANON))
      kcond_signal_one(&kb_input_cond);

   return KB_HANDLER_OK_AND_CONTINUE;
}

static void tty_handle_ctrl_plus_letter(u8 c)
{
   ASSERT(isalpha(c));
   char letter = toupper(c); /* ctrl ignores the case of the letter */
   char t = letter - 'A' + 1;
   kb_buf_write_elem(t);

   if (curr_termios.c_lflag & ECHO)
      term_write(&t, 1);
}

static int tty_handle_print_char_plus_ctrl_alt(u8 c)
{
   if (!isalpha(c)) {
       /* Ignore ctrl/alt + <non-letter> sequences for the moment. */
      return KB_HANDLER_NAK;
   }

   if (kb_is_alt_pressed()) {
      kb_buf_write_elem('\x1b');
      if (curr_termios.c_lflag & ECHO)
         term_write("\x1b", 1);
   }

   if (kb_is_ctrl_pressed()) {
      tty_handle_ctrl_plus_letter(c);
   } else {
      kb_buf_write_elem(c);
      if (curr_termios.c_lflag & ECHO)
         term_write((char *)&c, 1);
   }

   if (!(curr_termios.c_lflag & ICANON))
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

   if (curr_termios.c_lflag & ICANON) {
      if (c == 0x7f) {
         /*
          * In canonical (cooked) mode, translate the BACKSPACE key (ASCII DEL)
          * to \b (ASCII backspace)
          */
         c = '\b';
      }
   }

   if (!c)
      return tty_handle_non_printable_key(key);

   if (kb_is_ctrl_or_alt_pressed())
      return tty_handle_print_char_plus_ctrl_alt(c);

   if (curr_termios.c_lflag & ECHO)
      term_write((char *)&c, 1);

   if (curr_termios.c_lflag & ICANON)
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

   if (curr_termios.c_lflag & ICANON)
      term_set_col_offset(term_get_curr_col());

   do {

      while (kb_buf_is_empty()) {
         kcond_wait(&kb_input_cond, NULL, KCOND_WAIT_FOREVER);
      }

      while (read_count < size && !kb_buf_is_empty()) {
         buf[read_count++] = kb_buf_read_elem();
      }

      if (read_count > 0 && !(curr_termios.c_lflag & ICANON))
         break;

   } while (buf[read_count - 1] != '\n' || read_count == KB_INPUT_BUF_SIZE);

   return read_count;
}

static ssize_t tty_write(fs_handle h, char *buf, size_t size)
{
   // NOTE: the 'size' arg passed to term_write cannot be bigger than 1 MB.
   // TODO: call term_write() in a loop in order to handle size > 1 MB.

   term_write(buf, size);
   return size;
}

/* ----------------- Driver interface ---------------- */

int tty_ioctl(fs_handle h, uptr request, void *argp);

static int tty_create_device_file(int minor, file_ops *ops, devfs_entry_type *t)
{
   *t = DEVFS_CHAR_DEVICE;

   bzero(ops, sizeof(file_ops));

   ops->read = tty_read;
   ops->write = tty_write;
   ops->ioctl = tty_ioctl;
   ops->seek = NULL; /* seek() support is NOT mandatory, of course */
   return 0;
}

void init_tty(void)
{
   curr_termios = default_termios;
   driver_info *di = kmalloc(sizeof(driver_info));
   di->name = "tty";
   di->create_dev_file = tty_create_device_file;
   int major = register_driver(di);
   int rc = create_dev_file("tty", major, 0 /* minor */);

   if (rc != 0)
      panic("TTY: unable to create /dev/tty (error: %d)", rc);

   kcond_init(&kb_input_cond);
   ringbuf_init(&kb_input_ringbuf, KB_INPUT_BUF_SIZE, 1, kb_input_buf);

   if (kb_register_keypress_handler(&tty_keypress_handler) < 0)
      panic("TTY: unable to register keypress handler");

   term_set_filter_func(tty_term_write_filter, NULL);
}
