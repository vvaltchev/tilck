
#include <common/basic_defs.h>
#include <common/string_util.h>
#include <common/debug/termios_debug.c.h>

#include <exos/fs/exvfs.h>
#include <exos/errno.h>
#include <exos/user.h>
#include <exos/term.h>

#include <termios.h>      // system header
#include <sys/ioctl.h>    // system header

static void debug_dump_termios(struct termios *t)
{
   dump_c_iflag(t);
   dump_c_oflag(t);
   dump_c_cflag(t);
   dump_c_lflag(t);
   dump_c_cc(t);
}

struct termios curr_termios =
{
   .c_iflag = ICRNL | IXON,
   .c_oflag = OPOST | ONLCR,
   .c_cflag = CREAD,
   .c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE | IEXTEN,
   .c_line = 0,

   .c_cc =
   {
      0x3, 0x1c, 0x7f, 0x15, 0x4, 0x0, 0x1, 0x0,
      0x11, 0x13, 0x1a, 0x0, 0x12, 0xf, 0x17, 0x16,
      0x0, 0x0, 0x0
   }
};

static int tty_ioctl_tcgets(fs_handle h, void *argp)
{
   debug_dump_termios(&curr_termios);

   int rc = copy_to_user(argp, &curr_termios, sizeof(struct termios));

   if (rc < 0)
      return -EFAULT;

   return 0;
}

static int tty_ioctl_tiocgwinsz(fs_handle h, void *argp)
{
   struct winsize sz = {
      .ws_row = term_get_rows(),
      .ws_col = term_get_cols(),
      .ws_xpixel = 0,
      .ws_ypixel = 0
   };

   int rc = copy_to_user(argp, &sz, sizeof(struct winsize));

   if (rc < 0)
      return -EFAULT;

   return 0;
}

int tty_ioctl(fs_handle h, uptr request, void *argp)
{
   switch (request) {

      case TCGETS:
         return tty_ioctl_tcgets(h, argp);

      case TIOCGWINSZ:
         return tty_ioctl_tiocgwinsz(h, argp);

      default:
         printk("WARNING: unknown tty_ioctl() request: %p\n", request);
         return -EINVAL;
   }
}
