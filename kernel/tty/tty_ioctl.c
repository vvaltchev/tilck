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
      [VEOL2]     = 0,           /* typical value for TERM=linux (unset) */
   },
};

void tty_set_raw_mode(struct tty *t)
{
   disable_preemption();
   {
      tty_reset_termios(t);
      t->c_term.c_iflag &= (u32) ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
      t->c_term.c_oflag &= (u32) ~(OPOST);
      t->c_term.c_cflag |= (u32) (CS8);
      t->c_term.c_lflag &= (u32) ~(ECHO | ICANON | IEXTEN | ISIG);
   }
   enable_preemption();
}

void tty_set_medium_raw_mode(struct tty *t, bool enabled)
{
   disable_preemption();
   {
      tty_set_raw_mode(t);
      t->mediumraw_mode = enabled;
   }
   enable_preemption();
}

void tty_reset_termios(struct tty *t)
{
   disable_preemption();
   {
      t->c_term = default_termios;
      t->mediumraw_mode = false;
   }
   enable_preemption();
}

static int tty_ioctl_tcgets(struct tty *t, void *argp)
{
   int rc = copy_to_user(argp, &t->c_term, sizeof(struct termios));

   if (rc < 0)
      return -EFAULT;

   return 0;
}

static int tty_ioctl_tcsets(struct tty *t, void *argp)
{
   struct termios saved = t->c_term;
   int rc = copy_from_user(&t->c_term, argp, sizeof(struct termios));

   if (rc < 0) {
      t->c_term = saved;
      return -EFAULT;
   }

   tty_update_ctrl_handlers(t);
   tty_update_default_state_tables(t);
   return 0;
}

static int tty_ioctl_tiocgwinsz(struct tty *t, void *argp)
{
   struct winsize sz = {
      .ws_row = t->tparams.rows,
      .ws_col = t->tparams.cols,
      .ws_xpixel = 0,
      .ws_ypixel = 0,
   };

   int rc = copy_to_user(argp, &sz, sizeof(struct winsize));

   if (rc < 0)
      return -EFAULT;

   return 0;
}

void tty_setup_for_panic(struct tty *t)
{
   if (t->kd_gfx_mode != KD_TEXT) {

      /*
       * NOTE: don't try to always fully restart the video output
       * because it might trigger a nested panic. When kd_gfx_mode != KD_TEXT,
       * we have no other choice, if we wanna see something on the screen.
       *
       * TODO: investigate whether it is possible to make
       * term_restart_video_output() safer in panic scenarios.
       */
      t->tintf->restart_video_output(t->tstate);
      t->kd_gfx_mode = KD_TEXT;
   }
}

void tty_restore_kd_text_mode(struct tty *t)
{
   if (t->kd_gfx_mode == KD_TEXT)
      return;

   t->tintf->restart_video_output(t->tstate);
   t->kd_gfx_mode = KD_TEXT;
}

static int tty_ioctl_kdsetmode(struct tty *t, void *argp)
{
   uptr opt = (uptr) argp;

   if (opt == KD_TEXT) {
      tty_restore_kd_text_mode(t);
      return 0;
   }

   if (opt == KD_GRAPHICS) {
      t->tintf->pause_video_output(t->tstate);
      t->kd_gfx_mode = KD_GRAPHICS;
      return 0;
   }

   return -EINVAL;
}

static int tty_ioctl_KDGKBMODE(struct tty *t, void *argp)
{
   int mode = t->mediumraw_mode ? K_MEDIUMRAW : K_XLATE;

   if (!copy_to_user(argp, &mode, sizeof(int)))
      return 0;

   return -EFAULT;
}

static int tty_ioctl_KDSKBMODE(struct tty *t, void *argp)
{
   struct process *pi = get_curr_proc();

   if (t != pi->proc_tty)
      return -EPERM; /* don't allow setting mode of other TTYs */

   uptr mode = (uptr) argp;

   switch (mode) {

      case K_XLATE:
         t->mediumraw_mode = false;
         pi->did_set_tty_medium_raw = false;
         break;

      case K_MEDIUMRAW:
         t->mediumraw_mode = true;
         pi->did_set_tty_medium_raw = true;
         break;

      default:
         return -EINVAL;
   }

   return 0;
}

static int tty_ioctl_KDGKBTYPE(struct tty *t, void *argp)
{
   const int kb_type = KB_101;

   if (copy_to_user(argp, &kb_type, sizeof(kb_type)))
      return -EFAULT;

   return 0;
}

static int tty_ioctl_TIOCNOTTY(struct tty *t, void *argp)
{
   struct process *pi = get_curr_proc();

   if (pi->proc_tty != t)
      return -EPERM; /* that's not our controlling terminal */

   if (pi->sid == pi->pid) {

      /*
       * The session leader gives up his controlling terminal.
       *
       * TODO: send SIGHUP to all the processes in the foreground process group
       * and make all the processes in the current session to lose their
       * controlling terminal.
       */
   }

   pi->proc_tty = NULL;
   return 0;
}

static int tty_ioctl_TIOCSCTTY(struct tty *t, void *argp)
{
   struct process *pi = get_curr_proc();

   if (pi->proc_tty) {

      /*
       * The current process has already a controlling terminal.
       *
       * On Linux, if the caller process has the CAP_SYS_ADMIN capability and
       * arg equals 1, the terminal is stolen, and all processes that had it as
       * controlling terminal lose it.
       *
       * On Tilck, at least for the moment, that won't be supported.
       */

      return -EPERM;
   }

   if (pi->sid != pi->pid)
      return -EPERM; /* not a session leader */

   pi->proc_tty = t;
   return 0;
}

/* get foreground process group */
static int tty_ioctl_TIOCGPGRP(struct tty *t, int *user_pgrp)
{
   int rc;
   disable_preemption();
   {
      rc = t->fg_pgid;
   }
   enable_preemption();
   return rc;
}

/* set foregroup process group */
static int tty_ioctl_TIOCSPGRP(struct tty *t, const int *user_pgrp)
{
   struct process *pi = get_curr_proc();
   int pgid;
   int sid;

   if (pi->proc_tty != t)
      return -ENOTTY;

   if (copy_from_user(&pgid, user_pgrp, sizeof(int)))
      return -EFAULT;

   if (pgid < 0)
      return -EINVAL;

   sid = sched_get_session_of_group(pgid);

   if (sid < 0)
      return -EPERM; /* no such process group */

   if (sid != pi->sid)
      return -EPERM; /* progress group in a different session */

   disable_preemption();
   {
      t->fg_pgid = pgid;
   }
   enable_preemption();
   return 0;
}

int
tty_ioctl_int(struct tty *t, struct devfs_handle *h, uptr request, void *argp)
{
   switch (request) {

      case TCGETS:
         return tty_ioctl_tcgets(t, argp);

      case TCSETS: // equivalent to: tcsetattr(fd, TCSANOW, argp)
         return tty_ioctl_tcsets(t, argp);

      case TCSETSW: // equivalent to: tcsetattr(fd, TCSADRAIN, argp)
         return tty_ioctl_tcsets(t, argp);

      case TCSETSF: // equivalent to: tcsetattr(fd, TCSAFLUSH, argp)
         tty_inbuf_reset(t);
         return tty_ioctl_tcsets(t, argp);

      case TIOCGWINSZ:
         return tty_ioctl_tiocgwinsz(t, argp);

      case KDSETMODE:
         return tty_ioctl_kdsetmode(t, argp);

      case KDGKBMODE:
         return tty_ioctl_KDGKBMODE(t, argp);

      case KDSKBMODE:
         return tty_ioctl_KDSKBMODE(t, argp);

      case KDGKBTYPE:
         return tty_ioctl_KDGKBTYPE(t, argp);

      case TIOCSCTTY:
         return tty_ioctl_TIOCSCTTY(t, argp);

      case TIOCNOTTY:
         return tty_ioctl_TIOCNOTTY(t, argp);

      case TIOCGPGRP:
         return tty_ioctl_TIOCGPGRP(t, argp);

      case TIOCSPGRP:
         return tty_ioctl_TIOCSPGRP(t, argp);

      default:
         printk("WARNING: unknown tty_ioctl() request: %p\n", request);
         return -EINVAL;
   }
}
