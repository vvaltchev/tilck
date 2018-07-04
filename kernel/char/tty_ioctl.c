
#include <exos/common/basic_defs.h>
#include <exos/common/string_util.h>
#include <exos/common/debug/termios_debug.c.h>

#include <exos/kernel/fs/exvfs.h>
#include <exos/kernel/errno.h>
#include <exos/kernel/user.h>
#include <exos/kernel/term.h>

#include <termios.h>      // system header
#include <sys/ioctl.h>    // system header

struct termios curr_termios;

const struct termios default_termios =
{
   .c_iflag = ICRNL | IXON,
   .c_oflag = OPOST | ONLCR,
   .c_cflag = CREAD,
   .c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE | IEXTEN,
   .c_line = 0,

   .c_cc =
   {
      [VINTR]     = 0x03,
      [VQUIT]     = 0x1c,
      [VERASE]    = 0x7f,
      [VKILL]     = 0x15,
      [VEOF]      = 0x04,
      [VTIME]     = 0,
      [VMIN]      = 0x01,
      [VSWTC]     = 0,
      [VSTART]    = 0x11,
      [VSTOP]     = 0x13,
      [VSUSP]     = 0x1a,
      [VEOL]      = 0,
      [VREPRINT]  = 0x12,
      [VDISCARD]  = 0x0f,
      [VWERASE]   = 0x17,
      [VLNEXT]    = 0x16,
      [VEOL2]     = 0
   }
};

static int tty_ioctl_tcgets(fs_handle h, void *argp)
{
   //debug_dump_termios(&curr_termios);

   int rc = copy_to_user(argp, &curr_termios, sizeof(struct termios));

   if (rc < 0)
      return -EFAULT;

   return 0;
}

static int tty_ioctl_tcsets(fs_handle h, void *argp)
{
   struct termios saved = curr_termios;
   int rc = copy_from_user(&curr_termios, argp, sizeof(struct termios));

   if (rc < 0) {
      curr_termios = saved;
      return -EFAULT;
   }

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

      case TCSETS:
         return tty_ioctl_tcsets(h, argp);

      case TCSETSW:
         // TODO: implement the correct behavior for TCSETSW
         return tty_ioctl_tcsets(h, argp);

      case TCSETSF:
         // TODO: implement the correct behavior for TCSETSF
         return tty_ioctl_tcsets(h, argp);

      case TIOCGWINSZ:
         return tty_ioctl_tiocgwinsz(h, argp);

      default:
         printk("WARNING: unknown tty_ioctl() request: %p\n", request);
         return -EINVAL;
   }
}
