/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/debug/termios_debug.c.h>

#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/fs/devfs.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/sys_types.h>

#include "tty_int.h"

const struct termios default_termios =
{
   .c_iflag = ICRNL | IXON,
   .c_oflag = OPOST | ONLCR,
   .c_cflag = CREAD | B38400 | CS8,
   .c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE | IEXTEN,
   .c_line = 0,

   .c_cc =
   {
      [VINTR]     = 0x03,        /* typical value for TERM=linux, Ctrl+C */
      [VQUIT]     = 0x1c,        /* typical value for TERM=linux, Ctrl+\ */
      [VERASE]    = 0x7f,        /* typical value for TERM=linux */
      [VKILL]     = 0x15,        /* typical value for TERM=linux, Ctrl+7 */
      [VEOF]      = 0x04,        /* typical value for TERM=linux, Ctrl+D */
      [VTIME]     = 0,           /* typical value for TERM=linux (unset) */
      [VMIN]      = 0x01,        /* typical value for TERM=linux */
      [VSWTC]     = 0,           /* typical value for TERM=linux (unsupported)*/
      [VSTART]    = 0x11,        /* typical value for TERM=linux, Ctrl+Q */
      [VSTOP]     = 0x13,        /* typical value for TERM=linux, Ctrl+S */
      [VSUSP]     = 0x1a,        /* typical value for TERM=linux, Ctrl+Z */
      [VEOL]      = 0,           /* typical value for TERM=linux (unset) */
      [VREPRINT]  = 0x12,        /* typical value for TERM=linux, Ctrl+R */
      [VDISCARD]  = 0x0f,        /* typical value for TERM=linux, Ctrl+O */
      [VWERASE]   = 0x17,        /* typical value for TERM=linux, Ctrl+W */
      [VLNEXT]    = 0x16,        /* typical value for TERM=linux, Ctrl+V */
      [VEOL2]     = 0            /* typical value for TERM=linux (unset) */
   }
};

static int tty_ioctl_tcgets(tty *t, void *argp)
{
   int rc = copy_to_user(argp, &t->c_term, sizeof(struct termios));

   if (rc < 0)
      return -EFAULT;

   return 0;
}

static int tty_ioctl_tcsets(tty *t, void *argp)
{
   struct termios saved = t->c_term;
   int rc = copy_from_user(&t->c_term, argp, sizeof(struct termios));

   if (rc < 0) {
      t->c_term = saved;
      return -EFAULT;
   }

   tty_update_special_ctrl_handlers(t);
   tty_update_default_state_tables(t);
   return 0;
}

static int tty_ioctl_tiocgwinsz(tty *t, void *argp)
{
   struct winsize sz = {
      .ws_row = term_get_rows(t->term_inst),
      .ws_col = term_get_cols(t->term_inst),
      .ws_xpixel = 0,
      .ws_ypixel = 0
   };

   int rc = copy_to_user(argp, &sz, sizeof(struct winsize));

   if (rc < 0)
      return -EFAULT;

   return 0;
}

void tty_setup_for_panic(tty *t)
{
   if (t->kd_mode != KD_TEXT) {

      /*
       * NOTE: don't try to always fully restart the video output
       * because it might trigger a nested panic. When kd_mode != KD_TEXT,
       * we have no other choice, if we wanna see something on the screen.
       *
       * TODO: investigate whether it is possible to make
       * term_restart_video_output() safer in panic scenarios.
       */
      term_restart_video_output(t->term_inst);
      t->kd_mode = KD_TEXT;
   }
}

void tty_restore_kd_text_mode(tty *t)
{
   if (t->kd_mode == KD_TEXT)
      return;

   term_restart_video_output(t->term_inst);
   t->kd_mode = KD_TEXT;
}

static int tty_ioctl_kdsetmode(tty *t, void *argp)
{
   uptr opt = (uptr) argp;

   if (opt == KD_TEXT) {
      tty_restore_kd_text_mode(t);
      return 0;
   }

   if (opt == KD_GRAPHICS) {
      term_pause_video_output(t->term_inst);
      t->kd_mode = KD_GRAPHICS;
      return 0;
   }

   return -EINVAL;
}

static int tty_ioctl_KDGKBMODE(tty *t, void *argp)
{
   int mode = K_XLATE; /* The only supported mode, at the moment */

   if (!copy_to_user(argp, &mode, sizeof(int)))
      return 0;

   return -EFAULT;
}

static int tty_ioctl_KDSKBMODE(tty *t, void *argp)
{
   uptr mode = (uptr) argp;

   if (mode == K_XLATE)
      return 0;  /* K_XLATE is the only supported mode, at the moment */

   return -EINVAL;
}

static int tty_ioctl_TIOCSCTTY(tty *t, void *argp)
{
   task_info *ti = get_curr_task();

   if (!ti->pi->proc_tty) {

      ti->pi->proc_tty = t;

   } else {

      // TODO: support this case

      /*
      If this terminal is already the controlling terminal of a different
      session group, then the ioctl fails with EPERM, unless the caller has the
      CAP_SYS_ADMIN capability and arg equals 1, in which case the terminal is
      stolen, and all processes that had it as controlling terminal lose it.
      */

      return -EPERM;
   }

   return 0;
}

/* get foreground process group */
static int tty_ioctl_TIOCGPGRP(tty *t, int *user_pgrp)
{
   return -EINVAL;
}

/* set foregroup process group */
static int tty_ioctl_TIOCSPGRP(tty *t, const int *user_pgrp)
{
   return -EINVAL;
}

int tty_ioctl_int(tty *t, devfs_file_handle *h, uptr request, void *argp)
{
   switch (request) {

      case TCGETS:
         return tty_ioctl_tcgets(t, argp);

      case TCSETS: // equivalent to: tcsetattr(fd, TCSANOW, argp)
         return tty_ioctl_tcsets(t, argp);

      case TCSETSW: // equivalent to: tcsetattr(fd, TCSADRAIN, argp)
         return tty_ioctl_tcsets(t, argp);

      case TCSETSF: // equivalent to: tcsetattr(fd, TCSAFLUSH, argp)
         tty_kb_buf_reset(t);
         return tty_ioctl_tcsets(t, argp);

      case TIOCGWINSZ:
         return tty_ioctl_tiocgwinsz(t, argp);

      case KDSETMODE:
         return tty_ioctl_kdsetmode(t, argp);

      case KDGKBMODE:
         return tty_ioctl_KDGKBMODE(t, argp);

      case KDSKBMODE:
         return tty_ioctl_KDSKBMODE(t, argp);

      case TIOCSCTTY:
         return tty_ioctl_TIOCSCTTY(t, argp);

      case TIOCGPGRP:
         return tty_ioctl_TIOCGPGRP(t, argp);

      case TIOCSPGRP:
         return tty_ioctl_TIOCSPGRP(t, argp);

      default:
         printk("WARNING: unknown tty_ioctl() request: %p\n", request);
         return -EINVAL;
   }
}

int tty_fcntl_int(tty *t, devfs_file_handle *h, int cmd, int arg)
{
   return -EINVAL;
}
