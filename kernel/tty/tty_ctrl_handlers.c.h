/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/signal.h>
#include <tilck/kernel/process.h>

static bool tty_ctrl_stop(struct tty *t, bool block)
{
   if (t->c_term.c_iflag & IXON) {
      // TODO: eventually support pause transmission, one day.
      return true;
   }

   return false;
}

static bool tty_ctrl_start(struct tty *t, bool block)
{
   if (t->c_term.c_iflag & IXON) {
      // TODO: eventually support pause transmission, one day.
      return true;
   }

   return false;
}

static bool tty_ctrl_intr(struct tty *t, bool block)
{
   if (t->c_term.c_lflag & ISIG) {
      tty_keypress_echo(t, (char)t->c_term.c_cc[VINTR]);
      tty_inbuf_reset(t);
      send_signal_to_group(t->fg_pgid, SIGINT);
      return true;
   }

   return false;
}

static bool tty_ctrl_susp(struct tty *t, bool block)
{
   if (t->c_term.c_lflag & ISIG) {

      tty_keypress_echo(t, (char)t->c_term.c_cc[VSUSP]);

      if (!t->serial_port_fwd)
         printk("SUSP not supported yet\n");

      return true;
   }

   return false;
}

static bool tty_ctrl_quit(struct tty *t, bool block)
{
   if (t->c_term.c_lflag & ISIG) {
      tty_keypress_echo(t, (char)t->c_term.c_cc[VQUIT]);
      tty_inbuf_reset(t);
      send_signal_to_group(t->fg_pgid, SIGQUIT);
      return true;
   }

   return false;
}

static bool tty_ctrl_eof(struct tty *t, bool block)
{
   if (t->c_term.c_lflag & ICANON) {
      t->end_line_delim_count++;
      tty_inbuf_write_elem(t, t->c_term.c_cc[VEOF], block);
      kcond_signal_one(&t->input_cond);
      return true;
   }

   return false;
}

static bool tty_ctrl_eol(struct tty *t, bool block)
{
   if (t->c_term.c_lflag & ICANON) {
      t->end_line_delim_count++;
      tty_inbuf_write_elem(t, t->c_term.c_cc[VEOL], block);
      kcond_signal_one(&t->input_cond);
      return true;
   }
   return false;
}

static bool tty_ctrl_eol2(struct tty *t, bool block)
{
   if (t->c_term.c_lflag & ICANON) {
      t->end_line_delim_count++;
      tty_inbuf_write_elem(t, t->c_term.c_cc[VEOL2], block);
      kcond_signal_one(&t->input_cond);
      return true;
   }
   return false;
}

static bool tty_ctrl_reprint(struct tty *t, bool block)
{
   if (t->c_term.c_lflag & (ICANON | IEXTEN)) {

      tty_keypress_echo(t, (char)t->c_term.c_cc[VREPRINT]);

      if (!t->serial_port_fwd)
         printk("REPRINT not supported yet\n");

      return true;
   }

   return false;
}

static bool tty_ctrl_discard(struct tty *t, bool block)
{
   /*
    * From termios' man page:
    * VDISCARD
    *     (not  in  POSIX;  not supported under Linux; 017, SI, Ctrl-O)
    *     Toggle: start/stop discarding pending output.  Recognized when
    *     IEXTEN is set, and then not passed as input.
    *
    * Since it is not supported under Linux, it won't be supported under
    * Tilck either.
    */

   return false;
}

static bool tty_ctrl_lnext(struct tty *t, bool block)
{
   if (t->c_term.c_lflag & IEXTEN) {

      if (!t->serial_port_fwd)
         printk("LNEXT not supported yet\n");

      return true;
   }

   return false;
}

static void
tty_set_ctrl_handler(struct tty *t, u8 ctrl_type, tty_ctrl_sig_func h)
{
   u8 c = t->c_term.c_cc[ctrl_type];

   if (!c) {
      /*
       * The character #num associated with the control char 'ctrl_type' is 0:
       * this means that the user doesn't what the terminal to support that
       * specific control character at all. We cannot associate any handler with
       * it.
       */
      return;
   }

   t->ctrl_handlers[c] = h;
}

static inline bool
tty_handle_special_controls(struct tty *t, u8 c, bool block)
{
   tty_ctrl_sig_func handler = t->ctrl_handlers[c];

   if (handler)
      return handler(t, block);

   return false;
}
