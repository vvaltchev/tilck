/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/fs/devfs.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/kb.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/cmdline.h>

#include <termios.h>      // system header
#include <fcntl.h>        // system header
#include <linux/kd.h>     // system header

#include "tty_int.h"

static inline bool tty_inbuf_write_elem(struct tty *t, u8 c);
static void tty_keypress_echo(struct tty *t, char c);

#include "tty_ctrl_handlers.c.h"

static void tty_keypress_echo(struct tty *t, char c)
{
   struct termios *const c_term = &t->c_term;

   if (t->serial_port_fwd)
      return;

   if (t->kd_gfx_mode == KD_GRAPHICS)
      return;

   if (c == '\n' && (c_term->c_lflag & ECHONL)) {
      /*
       * From termios' man page:
       *
       *    ECHONL: If ICANON is also set, echo the NL character even if ECHO
       *            is not set.
       */
      t->tintf->write(t->tstate, &c, 1, t->curr_color);
      return;
   }

   if (!(c_term->c_lflag & ECHO)) {
      /* If ECHO is not enabled, just don't echo. */
      return;
   }

   /* echo is enabled */

   if (c_term->c_lflag & ICANON) {

      if (c == c_term->c_cc[VEOF]) {
         /* In canonical mode, EOF is never echoed */
         return;
      }

      if (c_term->c_lflag & ECHOK) {
         if (c == c_term->c_cc[VKILL]) {
            t->tintf->write(t->tstate, &c, 1, t->curr_color);
            return;
         }
      }

      if (c_term->c_lflag & ECHOE) {

        /*
         * From termios' man page:
         *
         *    ECHOE
         *        If ICANON is also set, the ERASE character erases the
         *        preceding input character, and WERASE erases the preceding
         *        word.
         */


         if (c == c_term->c_cc[VWERASE] || c == c_term->c_cc[VERASE]) {
            t->tintf->write(t->tstate, &c, 1, t->curr_color);
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
    *          special character. For example, character 0x08 (BS) is echoed
    *          as ^H.
    *
    */
   if ((c < ' ' || c == 0x7F) && (c_term->c_lflag & ECHOCTL)) {
      if (c != '\t' && c != '\n') {
         if (c != c_term->c_cc[VSTART] && c != c_term->c_cc[VSTOP]) {
            char mini_buf[2] = { '^', c + 0x40 };
            t->tintf->write(t->tstate, mini_buf, 2, t->curr_color);
            return;
         }
      }
   }

   /* Just ECHO a regular character */
   t->tintf->write(t->tstate, &c, 1, t->curr_color);
}

static inline bool tty_inbuf_is_empty(struct tty *t)
{
   bool ret;
   disable_preemption();
   {
      ret = ringbuf_is_empty(&t->input_ringbuf);
   }
   enable_preemption();
   return ret;
}

void tty_inbuf_reset(struct tty *t)
{
   disable_preemption();
   {
      ringbuf_reset(&t->input_ringbuf);
      t->end_line_delim_count = 0;
   }
   enable_preemption();
}

static inline u8 tty_inbuf_read_elem(struct tty *t)
{
   u8 ret = 0;
   disable_preemption();
   {
      ASSERT(!tty_inbuf_is_empty(t));
      DEBUG_CHECKED_SUCCESS(ringbuf_read_elem1(&t->input_ringbuf, &ret));
   }
   enable_preemption();
   return ret;
}

static inline bool tty_inbuf_drop_last_written_elem(struct tty *t)
{
   bool ret;
   tty_keypress_echo(t, (char)t->c_term.c_cc[VERASE]);

   disable_preemption();
   {
      ret = ringbuf_unwrite_elem(&t->input_ringbuf, NULL);
   }
   enable_preemption();
   return ret;
}

static inline bool tty_inbuf_write_elem(struct tty *t, u8 c)
{
   bool ret;
   tty_keypress_echo(t, (char)c);

   disable_preemption();
   {
      ret = ringbuf_write_elem1(&t->input_ringbuf, c);
   }
   enable_preemption();
   return ret;
}

static int
tty_handle_non_printable_key(struct kb_dev *kb, struct tty *t, u32 key)
{
   char seq[16];
   const u8 modifiers = kb_get_current_modifiers(kb);
   const bool found = kb->scancode_to_ansi_seq(key, modifiers, seq);
   const char *p = seq;

   if (!found) {
      /* Unknown/unsupported sequence: just do nothing avoiding weird effects */
      return kb_handler_nak;
   }

   while (*p) {
      tty_inbuf_write_elem(t, (u8) *p++);
   }

   if (!(t->c_term.c_lflag & ICANON))
      kcond_signal_one(&t->input_cond);

   return kb_handler_ok_and_continue;
}

static inline bool tty_is_line_delim_char(struct tty *t, u8 c)
{
   return c == '\n' ||
          c == t->c_term.c_cc[VEOF] ||
          c == t->c_term.c_cc[VEOL] ||
          c == t->c_term.c_cc[VEOL2];
}

static void
tty_keypress_handle_canon_mode(struct tty *t, u32 key, u8 c)
{
   if (c == t->c_term.c_cc[VERASE]) {

      tty_inbuf_drop_last_written_elem(t);

   } else {

      tty_inbuf_write_elem(t, c);

      if (tty_is_line_delim_char(t, c)) {
         t->end_line_delim_count++;
         kcond_signal_one(&t->input_cond);
      }
   }
}

void tty_send_keyevent(struct tty *t, struct key_event ke)
{
   u8 c = (u8)ke.print_char;

   if (c == '\r') {

      if (t->c_term.c_iflag & IGNCR)
         return; /* ignore the carriage return */

      if (t->c_term.c_iflag & ICRNL)
         c = '\n';

   } else if (c == '\n') {

      if (t->c_term.c_iflag & INLCR)
         c = '\r';
   }

   /* Ctrl+C, Ctrl+D, Ctrl+Z etc.*/
   if (tty_handle_special_controls(t, c))
      return;

   if (t->c_term.c_lflag & ICANON) {
      tty_keypress_handle_canon_mode(t, ke.key, c);
      return;
   }

   /* raw mode input handling */
   tty_inbuf_write_elem(t, c);
   kcond_signal_one(&t->input_cond);
}

static int
tty_keypress_handler_int(struct tty *t,
                         struct kb_dev *kb,
                         struct key_event ke)
{
   u8 c = (u8)ke.print_char;
   ASSERT(kb != NULL);

   if (!c)
      return tty_handle_non_printable_key(kb, t, ke.key);

   if (kb_is_alt_pressed(kb))
      tty_inbuf_write_elem(t, '\033');

   if (kb_is_ctrl_pressed(kb)) {
      if (isalpha(c) || c == '\\' || c == '[') {
         /* ctrl ignores the case of the letter */
         c = (u8)(toupper(c) - 'A' + 1);
      }
   }

   ke.print_char = (char)c;
   tty_send_keyevent(t, ke);
   return kb_handler_ok_and_continue;
}

int set_curr_tty(struct tty *t)
{
   int res = -EPERM;
   disable_preemption();
   {
      if (__curr_tty->kd_gfx_mode == KD_TEXT) {

         __curr_tty = t;

         if (t->tintf->get_type() == term_type_video)
            set_curr_video_term(t->tstate);

         res = 0;
      }
   }
   enable_preemption();
   return res;
}

enum kb_handler_action
tty_keypress_handler(struct kb_dev *kb, struct key_event ke)
{
   struct tty *const t = get_curr_tty();
   const u32 key = ke.key;

   if (t->mediumraw_mode) {

      const u8 mr = kb->translate_to_mediumraw(ke);

      if (!mr) {

         /*
          * For any reason, we don't have a MEDIUMRAW translation for that key.
          * Just ignore the key press/release, that's it.
          */
         return kb_handler_ok_and_stop;
      }

      tty_inbuf_write_elem(t, mr);
      kcond_signal_one(&t->input_cond);
      return kb_handler_ok_and_stop;
   }

   if (!ke.pressed)
      return kb_handler_nak;

   if (key == KEY_PAGE_UP && kb_is_shift_pressed(kb)) {
      t->tintf->scroll_up(t->tstate, TERM_SCROLL_LINES);
      return kb_handler_ok_and_stop;
   }

   if (key == KEY_PAGE_DOWN && kb_is_shift_pressed(kb)) {
      t->tintf->scroll_down(t->tstate, TERM_SCROLL_LINES);
      return kb_handler_ok_and_stop;
   }

   if (kb_is_alt_pressed(kb)) {

      struct tty *other_tty;
      int fn = kb_get_fn_key_pressed(key);

      if (fn > 0 && t->kd_gfx_mode == KD_TEXT) {

         if (fn > kopt_tty_count)
            return kb_handler_ok_and_stop; /* just ignore the key stroke */

         other_tty = ttys[fn];

         if (other_tty == t)
            return kb_handler_ok_and_stop; /* just ignore the key stroke */

         ASSERT(other_tty != NULL);

         set_curr_tty(other_tty);
         return kb_handler_ok_and_stop;
      }
   }

   return tty_keypress_handler_int(t, kb, ke);
}

static size_t tty_flush_read_buf(struct devfs_handle *h, char *buf, size_t size)
{
   ssize_t rem = h->read_buf_used - h->read_pos;
   ASSERT(rem >= 0);

   size_t m = MIN((size_t)rem, size);
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
tty_internal_read_single_char_from_kb(struct tty *t,
                                      struct devfs_handle *h,
                                      bool *delim_break)
{
   u8 c = tty_inbuf_read_elem(t);
   h->read_buf[h->read_buf_used++] = (char)c;

   if (t->c_term.c_lflag & ICANON) {

      if (tty_is_line_delim_char(t, c)) {
         ASSERT(t->end_line_delim_count > 0);
         t->end_line_delim_count--;
         *delim_break = true;

         /* All line delimiters except EOF are kept */
         if (c == t->c_term.c_cc[VEOF])
            h->read_buf_used--;
      }

      return !*delim_break;
   }

   /*
    * In raw mode it makes no sense to read until a line delim is
    * found: we should read the minimum necessary.
    */
   return !(h->read_buf_used >= t->c_term.c_cc[VMIN]);
}

static inline bool
tty_internal_should_read_return(struct tty *t,
                                struct devfs_handle *h,
                                size_t read_cnt,
                                bool delim_break)
{
   if (t->c_term.c_lflag & ICANON) {
      return
         delim_break ||
            (t->end_line_delim_count > 0 &&
               (h->read_buf_used == DEVFS_READ_BS || read_cnt == TTY_INPUT_BS));
   }

   /* Raw mode handling */
   return read_cnt >= t->c_term.c_cc[VMIN];
}

bool tty_read_ready_int(struct tty *t, struct devfs_handle *h)
{
   if (t->c_term.c_lflag & ICANON) {
      return h->read_allowed_to_return || t->end_line_delim_count > 0;
   }

   /* Raw mode handling */
   return ringbuf_get_elems(&t->input_ringbuf) >= t->c_term.c_cc[VMIN];
}

ssize_t
tty_read_int(struct tty *t, struct devfs_handle *h, char *buf, size_t size)
{
   struct process *pi = get_curr_proc();
   size_t read_count = 0;
   bool delim_break;

   ASSERT(is_preemption_enabled());

   if (pi->proc_tty != t) {

      /*
       * Cannot read from this TTY, as it is not the process' controlling
       * terminal.
       */
      return -EIO;
   }

   if (pi->pgid != t->fg_pgid) {

      /*
       * Cannot read from TTY, as the process is not in the terminal's
       * foreground process group.
       */

      return -EIO;
   }

   if (!size)
      return 0;

   if (h->read_buf_used) {

      if (!(h->fl_flags & O_NONBLOCK))
         return (ssize_t) tty_flush_read_buf(h, buf, size);

      /*
       * The file description is in NON-BLOCKING mode: this means we cannot
       * just return the buffer to the user even if there is something left in
       * it because the tty might be in canonical mode (and we're not sure the
       * user pressed ENTER). Therefore, we have to check an additional flag
       * called read_allowed_to_return that is set if the user actually pressed
       * ENTER (precisely: a line delimiter has been written to the tty). In
       * the BLOCKING mode case (default), we can, instead, actually flush the
       * buffer and return because read_buf_used > 0 means just that user's
       * buffer was just not big enough.
       */

      if (h->read_allowed_to_return) {

         ssize_t ret = (ssize_t) tty_flush_read_buf(h, buf, size);

         if (!h->read_buf_used)
            h->read_allowed_to_return = false;

         return ret;
      }
   }

   if (t->c_term.c_lflag & ICANON)
      t->tintf->set_col_offset(t->tstate, -1 /* current col */);

   h->read_allowed_to_return = false;

   do {

      if ((h->fl_flags & O_NONBLOCK) && tty_inbuf_is_empty(t))
         return -EAGAIN;

      while (tty_inbuf_is_empty(t)) {
         kcond_wait(&t->input_cond, NULL, KCOND_WAIT_FOREVER);
      }

      delim_break = false;

      if (!(h->fl_flags & O_NONBLOCK)) {
         ASSERT(h->read_buf_used == 0);
         ASSERT(h->read_pos == 0);
      }

      while (!tty_inbuf_is_empty(t) &&
             h->read_buf_used < DEVFS_READ_BS &&
             tty_internal_read_single_char_from_kb(t, h, &delim_break)) { }

      if (!(h->fl_flags & O_NONBLOCK) || !(t->c_term.c_lflag & ICANON))
         read_count += tty_flush_read_buf(h, buf+read_count, size-read_count);

      ASSERT(t->end_line_delim_count >= 0);

   } while (!tty_internal_should_read_return(t, h, read_count, delim_break));

   if (h->fl_flags & O_NONBLOCK) {

      /*
       * If we got here in NONBLOCK mode, that means we exited the loop properly
       * with tty_internal_should_read_return() returning true. Now we have to
       * flush the read buffer.
       */

      read_count += tty_flush_read_buf(h, buf+read_count, size-read_count);

      if (h->read_buf_used)
         h->read_allowed_to_return = true;
   }

   return (ssize_t) read_count;
}

void tty_update_ctrl_handlers(struct tty *t)
{
   bzero(t->ctrl_handlers, 256 * sizeof(tty_ctrl_sig_func));
   tty_set_ctrl_handler(t, VSTOP, tty_ctrl_stop);
   tty_set_ctrl_handler(t, VSTART, tty_ctrl_start);
   tty_set_ctrl_handler(t, VINTR, tty_ctrl_intr);
   tty_set_ctrl_handler(t, VSUSP, tty_ctrl_susp);
   tty_set_ctrl_handler(t, VQUIT, tty_ctrl_quit);
   tty_set_ctrl_handler(t, VEOF, tty_ctrl_eof);
   tty_set_ctrl_handler(t, VEOL, tty_ctrl_eol);
   tty_set_ctrl_handler(t, VEOL2, tty_ctrl_eol2);
   tty_set_ctrl_handler(t, VREPRINT, tty_ctrl_reprint);
   tty_set_ctrl_handler(t, VDISCARD, tty_ctrl_discard);
   tty_set_ctrl_handler(t, VLNEXT, tty_ctrl_lnext);
}

void tty_input_init(struct tty *t)
{
   kcond_init(&t->input_cond);
   ringbuf_init(&t->input_ringbuf, TTY_INPUT_BS, 1, t->input_buf);
   tty_update_ctrl_handlers(t);
}
