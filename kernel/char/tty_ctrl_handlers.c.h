

typedef bool (*tty_ctrl_sig_func)(void);
static tty_ctrl_sig_func tty_special_ctrl_handlers[256];

static bool tty_ctrl_stop(void)
{
   if (c_term.c_iflag & IXON) {
      // TODO: eventually support pause transmission, one day.
      return true;
   }

   return false;
}

static bool tty_ctrl_start(void)
{
   if (c_term.c_iflag & IXON) {
      // TODO: eventually support pause transmission, one day.
      return true;
   }

   return false;
}

static bool tty_ctrl_intr(void)
{
   if (c_term.c_lflag & ISIG) {
      printk("INTR not supported yet\n");
      return true;
   }

   return false;
}

static bool tty_ctrl_susp(void)
{
   if (c_term.c_lflag & ISIG) {
      printk("SUSP not supported yet\n");
      return true;
   }

   return false;
}

static bool tty_ctrl_quit(void)
{
   if (c_term.c_lflag & ISIG) {
      printk("QUIT not supported yet\n");
      return true;
   }

   return false;
}

static bool tty_ctrl_eof(void)
{
   if (c_term.c_lflag & ICANON) {
      kb_buf_write_elem(c_term.c_cc[VEOF]);
      kcond_signal_one(&kb_input_cond);
      return true;
   }

   return false;
}

static bool tty_ctrl_eol(void)
{
   if (c_term.c_lflag & ICANON) {
      kb_buf_write_elem(c_term.c_cc[VEOL]);
      kcond_signal_one(&kb_input_cond);
      return true;
   }
   return false;
}

static bool tty_ctrl_eol2(void)
{
   if (c_term.c_lflag & ICANON) {
      kb_buf_write_elem(c_term.c_cc[VEOL2]);
      kcond_signal_one(&kb_input_cond);
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
}

static bool tty_handle_special_controls(u8 c)
{
   tty_ctrl_sig_func handler = tty_special_ctrl_handlers[c];

   if (handler)
      return handler();

   return false;
}
