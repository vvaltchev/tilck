/* SPDX-License-Identifier: BSD-2-Clause */

static bool tty_ctrl_stop(tty *t)
{
   if (c_term.c_iflag & IXON) {
      // TODO: eventually support pause transmission, one day.
      return true;
   }

   return false;
}

static bool tty_ctrl_start(tty *t)
{
   if (c_term.c_iflag & IXON) {
      // TODO: eventually support pause transmission, one day.
      return true;
   }

   return false;
}

static bool tty_ctrl_intr(tty *t)
{
   if (c_term.c_lflag & ISIG) {
      printk("INTR not supported yet\n");
      return true;
   }

   return false;
}

static bool tty_ctrl_susp(tty *t)
{
   if (c_term.c_lflag & ISIG) {
      printk("SUSP not supported yet\n");
      return true;
   }

   return false;
}

static bool tty_ctrl_quit(tty *t)
{
   if (c_term.c_lflag & ISIG) {
      printk("QUIT not supported yet\n");
      return true;
   }

   return false;
}

static bool tty_ctrl_eof(tty *t)
{
   if (c_term.c_lflag & ICANON) {
      t->tty_end_line_delim_count++;
      kb_buf_write_elem(t, c_term.c_cc[VEOF]);
      kcond_signal_one(&t->kb_input_cond);
      return true;
   }

   return false;
}

static bool tty_ctrl_eol(tty *t)
{
   if (c_term.c_lflag & ICANON) {
      t->tty_end_line_delim_count++;
      kb_buf_write_elem(t, c_term.c_cc[VEOL]);
      kcond_signal_one(&t->kb_input_cond);
      return true;
   }
   return false;
}

static bool tty_ctrl_eol2(tty *t)
{
   if (c_term.c_lflag & ICANON) {
      t->tty_end_line_delim_count++;
      kb_buf_write_elem(t, c_term.c_cc[VEOL2]);
      kcond_signal_one(&t->kb_input_cond);
      return true;
   }
   return false;
}

static bool tty_ctrl_reprint(tty *t)
{
   if (c_term.c_lflag & (ICANON | IEXTEN)) {
      printk("REPRINT not supported yet\n");
      return true;
   }

   return false;
}

static bool tty_ctrl_discard(tty *t)
{
   if (c_term.c_lflag & IEXTEN) {
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
      return true;
   }

   return false;
}

static bool tty_ctrl_lnext(tty *t)
{
   if (c_term.c_lflag & IEXTEN) {
      printk("LNEXT not supported yet\n");
      return true;
   }

   return false;
}

static void tty_set_ctrl_handler(u8 ctrl_type, tty_ctrl_sig_func h)
{
   u8 c = c_term.c_cc[ctrl_type];

   if (!c) {
      /*
       * The character #num associated with the control char 'ctrl_type' is 0:
       * this means that the user doesn't what the terminal to support that
       * specific control character at all. We cannot associate any handler with
       * it.
       */
      return;
   }

   tty_special_ctrl_handlers[c] = h;
}

void tty_update_special_ctrl_handlers(void)
{
   bzero(tty_special_ctrl_handlers, sizeof(tty_special_ctrl_handlers));
   tty_set_ctrl_handler(VSTOP, tty_ctrl_stop);
   tty_set_ctrl_handler(VSTART, tty_ctrl_start);
   tty_set_ctrl_handler(VINTR, tty_ctrl_intr);
   tty_set_ctrl_handler(VSUSP, tty_ctrl_susp);
   tty_set_ctrl_handler(VQUIT, tty_ctrl_quit);
   tty_set_ctrl_handler(VEOF, tty_ctrl_eof);
   tty_set_ctrl_handler(VEOL, tty_ctrl_eol);
   tty_set_ctrl_handler(VEOL2, tty_ctrl_eol2);
   tty_set_ctrl_handler(VREPRINT, tty_ctrl_reprint);
   tty_set_ctrl_handler(VDISCARD, tty_ctrl_discard);
   tty_set_ctrl_handler(VLNEXT, tty_ctrl_lnext);
}

static bool tty_handle_special_controls(tty *t, u8 c)
{
   tty_ctrl_sig_func handler = tty_special_ctrl_handlers[c];

   if (handler)
      return handler(t);

   return false;
}
