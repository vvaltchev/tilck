/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Userspace dump callbacks for ptype_ioctl_argp and ptype_fcntl_arg
 * — the context-dependent struct pointers whose layout depends on
 * a sibling cmd/request value.
 *
 * The kernel save side (modules/tracing/ptype_argp.c) decided
 * which bytes to copy from userspace based on the request value;
 * here we mirror the same switch to interpret the saved bytes as
 * the right struct.
 *
 * Tilck's implemented ioctls live in kernel/tty/tty_ioctl.c —
 * tty + console + a couple of fb-adjacent commands. Both this
 * file and modules/tracing/ptype_argp.c carry independent copies
 * of the cmd values; keep them in sync.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <termios.h>          /* struct termios */
#include <sys/ioctl.h>        /* struct winsize */

#include <tilck/common/tracing/wire.h>

#include "tr.h"

/* Mirror the cmd constants from modules/tracing/ptype_argp.c. */
#define IOCTL_TCGETS         0x5401UL
#define IOCTL_TCSETS         0x5402UL
#define IOCTL_TCSETSW        0x5403UL
#define IOCTL_TCSETSF        0x5404UL
#define IOCTL_TIOCGPGRP      0x540FUL
#define IOCTL_TIOCSPGRP      0x5410UL
#define IOCTL_TIOCGWINSZ     0x5413UL
#define IOCTL_TIOCSWINSZ     0x5414UL
#define IOCTL_KDGKBTYPE      0x4B33UL
#define IOCTL_KDSETMODE      0x4B3AUL
#define IOCTL_KDGKBMODE      0x4B44UL
#define IOCTL_KDSKBMODE      0x4B45UL

#define FCNTL_F_DUPFD            0UL
#define FCNTL_F_SETFD            2UL
#define FCNTL_F_GETFD            1UL
#define FCNTL_F_GETFL            3UL
#define FCNTL_F_SETFL            4UL
#define FCNTL_F_DUPFD_CLOEXEC    1030UL

/* Mirror layout from save side: matches struct termios on i386 (we
 * captured 44 bytes, conservative for the 36-byte i386 struct). */
struct termios_summary {
   unsigned c_iflag;
   unsigned c_oflag;
   unsigned c_cflag;
   unsigned c_lflag;
   unsigned char c_line;
   unsigned char c_cc[19];
};

/* dump_ioctl_argp: data points into the saved-params slot; data_size
 * tells us how much was copied. The helper is the request value. */
bool tr_dump_ioctl_argp(unsigned long orig,
                        char *data, long data_size,
                        long helper,
                        char *dst, size_t bs)
{
   const unsigned long req = (unsigned long)helper;

   /* NULL argp is rendered as NULL regardless of cmd. */
   if (!orig) {
      int rc = snprintf(dst, bs, "NULL");
      return rc >= 0 && (size_t)rc < bs;
   }

   /* The save callback may have stored "<fault>" if copy_from_user
    * failed. Pass that through. */
   if (data_size >= 7 && !memcmp(data, "<fault>", 7)) {
      int rc = snprintf(dst, bs, "<fault>");
      return rc >= 0 && (size_t)rc < bs;
   }

   switch (req) {

      case IOCTL_TCGETS:
      case IOCTL_TCSETS:
      case IOCTL_TCSETSW:
      case IOCTL_TCSETSF: {

         const struct termios_summary *t =
            (const struct termios_summary *)data;

         /* Render the four flag words + the most-useful c_cc
          * positions (VINTR=0, VQUIT=1, VERASE=2, VKILL=3, VEOF=4). */
         int rc = snprintf(
            dst, bs,
            "(struct termios){ .c_iflag = 0x%x, .c_oflag = 0x%x, "
            ".c_cflag = 0x%x, .c_lflag = 0x%x, "
            ".c_cc = {[VINTR]=0x%02x, [VQUIT]=0x%02x, [VERASE]=0x%02x} }",
            t->c_iflag, t->c_oflag, t->c_cflag, t->c_lflag,
            t->c_cc[0], t->c_cc[1], t->c_cc[2]);
         return rc >= 0 && (size_t)rc < bs;
      }

      case IOCTL_TIOCGWINSZ:
      case IOCTL_TIOCSWINSZ: {

         /* struct winsize: 4 × unsigned short. */
         const unsigned short *w = (const unsigned short *)data;
         int rc = snprintf(
            dst, bs,
            "(struct winsize){ .ws_row = %u, .ws_col = %u, "
            ".ws_xpixel = %u, .ws_ypixel = %u }",
            w[0], w[1], w[2], w[3]);
         return rc >= 0 && (size_t)rc < bs;
      }

      case IOCTL_TIOCGPGRP:
      case IOCTL_TIOCSPGRP:
      case IOCTL_KDGKBMODE:
      case IOCTL_KDSKBMODE:
      case IOCTL_KDGKBTYPE: {

         const int *p = (const int *)data;
         int rc = snprintf(dst, bs, "&%d", *p);
         return rc >= 0 && (size_t)rc < bs;
      }

      default:
         /* Unknown / unhandled cmd — render as raw pointer. */
         {
            int rc = snprintf(dst, bs, "%p", (void *)orig);
            return rc >= 0 && (size_t)rc < bs;
         }
   }
}

/* dump_fcntl_arg: register-value dispatch by cmd. data/data_size
 * are ignored because the kernel save callback (ptype_argp.c) is
 * a no-op for the cmds Tilck implements — F_DUPFD / F_SETFD /
 * F_SETFL etc. all take an int that's already in the arg register
 * (visible via `orig`); F_GETFD / F_GETFL ignore arg entirely. */
bool tr_dump_fcntl_arg(unsigned long orig,
                       char *data, long data_size,
                       long helper,
                       char *dst, size_t bs)
{
   const unsigned long cmd = (unsigned long)helper;
   int rc;

   switch (cmd) {

      case FCNTL_F_GETFD:
      case FCNTL_F_GETFL:
         rc = snprintf(dst, bs, "(unused)");
         break;

      case FCNTL_F_DUPFD:
      case FCNTL_F_DUPFD_CLOEXEC:
         rc = snprintf(dst, bs, "minfd=%lu", orig);
         break;

      case FCNTL_F_SETFD:
         rc = snprintf(dst, bs, "%s",
                       (orig & 1) ? "FD_CLOEXEC" : "0");
         break;

      case FCNTL_F_SETFL:
         /* arg is the new fl_flags — same shape as O_*. Delegate
          * to the open_flags renderer for parity. */
         return tr_dump_from_val(TR_PT_OPEN_FLAGS, orig, helper, dst, bs);

      default:
         rc = snprintf(dst, bs, "0x%lx", orig);
   }

   return rc >= 0 && (size_t)rc < bs;
}
