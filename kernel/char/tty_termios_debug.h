

#define PRINT_FLAG(val, flag) \
   if ((val) & flag) printk(NO_PREFIX #flag " ")

static void dump_c_iflag(struct termios *t)
{
   printk("c_iflag: ");
   PRINT_FLAG(t->c_iflag, IGNBRK);
   PRINT_FLAG(t->c_iflag, BRKINT);
   PRINT_FLAG(t->c_iflag, IGNPAR);
   PRINT_FLAG(t->c_iflag, PARMRK);
   PRINT_FLAG(t->c_iflag, INPCK);
   PRINT_FLAG(t->c_iflag, ISTRIP);
   PRINT_FLAG(t->c_iflag, INLCR);
   PRINT_FLAG(t->c_iflag, IGNCR);
   PRINT_FLAG(t->c_iflag, ICRNL);
   PRINT_FLAG(t->c_iflag, IUCLC);
   PRINT_FLAG(t->c_iflag, IXON);
   PRINT_FLAG(t->c_iflag, IXANY);
   PRINT_FLAG(t->c_iflag, IXOFF);
   PRINT_FLAG(t->c_iflag, IMAXBEL);
   PRINT_FLAG(t->c_iflag, IUTF8);
   printk(NO_PREFIX "\n");
}

static void dump_c_oflag(struct termios *t)
{
   printk("c_oflag: ");
   PRINT_FLAG(t->c_oflag, OPOST);
   PRINT_FLAG(t->c_oflag, OLCUC);
   PRINT_FLAG(t->c_oflag, ONLCR);
   PRINT_FLAG(t->c_oflag, OCRNL);
   PRINT_FLAG(t->c_oflag, ONOCR);
   PRINT_FLAG(t->c_oflag, ONLRET);
   PRINT_FLAG(t->c_oflag, OFILL);
   PRINT_FLAG(t->c_oflag, OFDEL);
   PRINT_FLAG(t->c_oflag, NLDLY);     // mask
   PRINT_FLAG(t->c_oflag, CRDLY);     // mask
   PRINT_FLAG(t->c_oflag, TABDLY);    // mask
   PRINT_FLAG(t->c_oflag, BSDLY);     // mask
   PRINT_FLAG(t->c_oflag, VTDLY);     // mask
   PRINT_FLAG(t->c_oflag, FFDLY);     // mask
   printk(NO_PREFIX "\n");
}

static void dump_c_cflag(struct termios *t)
{
   printk("c_oflag: ");
   PRINT_FLAG(t->c_cflag, CBAUD);    // mask
   PRINT_FLAG(t->c_cflag, CBAUDEX);  // mask
   PRINT_FLAG(t->c_cflag, CSIZE);    // mask
   PRINT_FLAG(t->c_cflag, CSTOPB);
   PRINT_FLAG(t->c_cflag, CREAD);
   PRINT_FLAG(t->c_cflag, PARENB);
   PRINT_FLAG(t->c_cflag, PARODD);
   PRINT_FLAG(t->c_cflag, HUPCL);
   PRINT_FLAG(t->c_cflag, CLOCAL);
   PRINT_FLAG(t->c_cflag, CIBAUD);   // mask
   PRINT_FLAG(t->c_cflag, CMSPAR);
   PRINT_FLAG(t->c_cflag, CRTSCTS);
   printk(NO_PREFIX "\n");
}

static void dump_c_lflag(struct termios *t)
{
   printk("c_lflag: ");
   PRINT_FLAG(t->c_lflag, ISIG);
   PRINT_FLAG(t->c_lflag, ICANON);
   PRINT_FLAG(t->c_lflag, XCASE);
   PRINT_FLAG(t->c_lflag, ECHO);
   PRINT_FLAG(t->c_lflag, ECHOE);
   PRINT_FLAG(t->c_lflag, ECHOK);
   PRINT_FLAG(t->c_lflag, ECHONL);
   PRINT_FLAG(t->c_lflag, ECHOCTL);
   PRINT_FLAG(t->c_lflag, ECHOPRT);
   PRINT_FLAG(t->c_lflag, ECHOKE);
   PRINT_FLAG(t->c_lflag, FLUSHO);
   PRINT_FLAG(t->c_lflag, NOFLSH);
   PRINT_FLAG(t->c_lflag, TOSTOP);
   PRINT_FLAG(t->c_lflag, PENDIN);
   PRINT_FLAG(t->c_lflag, IEXTEN);
   printk(NO_PREFIX "\n");
}

static void debug_dump_termios(struct termios *t)
{
   dump_c_iflag(t);
   dump_c_oflag(t);
   dump_c_cflag(t);
   dump_c_lflag(t);
}

