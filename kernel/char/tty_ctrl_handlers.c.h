

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
      /* Special write to the kb buf without calling kb_buf_write_elem */
      ringbuf_write_elem1(&kb_input_ringbuf, c_term.c_cc[VEOF]);
      kcond_signal_one(&kb_input_cond);
      return true;
   }

   return false;
}

void tty_update_special_ctrl_handlers(void)
{
   bzero(tty_special_ctrl_handlers, sizeof(tty_special_ctrl_handlers));
   tty_special_ctrl_handlers[c_term.c_cc[VSTOP]] = tty_ctrl_stop;
   tty_special_ctrl_handlers[c_term.c_cc[VSTART]] = tty_ctrl_start;
   tty_special_ctrl_handlers[c_term.c_cc[VINTR]] = tty_ctrl_intr;
   tty_special_ctrl_handlers[c_term.c_cc[VSUSP]] = tty_ctrl_susp;
   tty_special_ctrl_handlers[c_term.c_cc[VQUIT]] = tty_ctrl_quit;
   tty_special_ctrl_handlers[c_term.c_cc[VEOF]] = tty_ctrl_eof;
}

static bool tty_handle_special_controls(u8 c)
{
   tty_ctrl_sig_func handler = tty_special_ctrl_handlers[c];

   if (handler)
      return handler();

   return false;
}
