
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/fs/exvfs.h>
#include <exos/errno.h>
#include <exos/user.h>
#include <exos/term.h>

#include <termios.h>      // system header
#include <sys/ioctl.h>    // system header

struct termios curr_termios =
{
   0x4500,
   0x05,
   0xbf,
   0x8a3b,
   0,
   {
      0x3, 0x1c, 0x7f, 0x15, 0x4, 0x0, 0x1, 0x0,
      0x11, 0x13, 0x1a, 0x0, 0x12, 0xf, 0x17, 0x16,
      0x0, 0x0, 0x0
   },
   0, // ispeed
   0  // ospeed
};

static void debug_dump_termios(struct termios *t)
{
   printk(NO_PREFIX "a, ");
   printk(NO_PREFIX "b, ");
   printk(NO_PREFIX "c\n");
}

static int tty_ioctl_tcgets(fs_handle h, void *argp)
{
   printk("*********** tty_ioctl_tcgets ***********\n");
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
