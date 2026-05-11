/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Save callbacks for ptype_ioctl_argp and ptype_fcntl_arg —
 * context-dependent struct pointers. The `helper` argument (passed
 * via the metadata's `helper_param_name`) carries the sibling
 * cmd/request value; this callback switches on it to decide how
 * many bytes to copy_from_user.
 *
 * The matching dump callbacks (which render the saved bytes as
 * the cmd-specific struct) live in userapps/dp/tr_dump_ioctl.c.
 * Both sides must agree on cmd → struct-size mapping.
 */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/kernel/user.h>
#include <tilck/mods/tracing.h>

/* ioctl request constants — must match the userspace dispatcher
 * in tr_dump_ioctl.c. These are the only requests Tilck currently
 * handles (see kernel/tty/tty_ioctl.c). */
#define IOCTL_TCGETS         0x5401UL
#define IOCTL_TCSETS         0x5402UL
#define IOCTL_TCSETSW        0x5403UL
#define IOCTL_TCSETSF        0x5404UL
#define IOCTL_TIOCSCTTY      0x540EUL
#define IOCTL_TIOCGPGRP      0x540FUL
#define IOCTL_TIOCSPGRP      0x5410UL
#define IOCTL_TIOCGWINSZ     0x5413UL
#define IOCTL_TIOCSWINSZ     0x5414UL
#define IOCTL_TIOCNOTTY      0x5422UL
#define IOCTL_KDGKBTYPE      0x4B33UL
#define IOCTL_KDSETMODE      0x4B3AUL
#define IOCTL_KDGKBMODE      0x4B44UL
#define IOCTL_KDSKBMODE      0x4B45UL

/* fcntl cmd constants. */
#define FCNTL_F_DUPFD            0UL
#define FCNTL_F_GETFD            1UL
#define FCNTL_F_SETFD            2UL
#define FCNTL_F_GETFL            3UL
#define FCNTL_F_SETFL            4UL
#define FCNTL_F_DUPFD_CLOEXEC    1030UL

/* How many bytes to copy from argp into the slot, given the
 * ioctl request value. 0 means "no struct, argp is unused or is
 * an immediate int already in the register" — leave slot empty. */
static unsigned
ioctl_argp_save_size(unsigned long request)
{
   switch (request) {

      case IOCTL_TCGETS:
      case IOCTL_TCSETS:
      case IOCTL_TCSETSW:
      case IOCTL_TCSETSF:
         /* struct termios — 36 bytes on i386 musl (4*c_*flag + 1
          * c_line + NCCS c_cc[19] + padding). Conservative 44 fits
          * in the 64-byte slot. */
         return 44;

      case IOCTL_TIOCGWINSZ:
      case IOCTL_TIOCSWINSZ:
         /* struct winsize: 4 × unsigned short = 8 bytes. */
         return 8;

      case IOCTL_TIOCGPGRP:
      case IOCTL_TIOCSPGRP:
      case IOCTL_KDGKBMODE:
      case IOCTL_KDSKBMODE:
      case IOCTL_KDGKBTYPE:
         /* int *  — 4 bytes (i386) on these. */
         return 4;

      default:
         return 0;
   }
}

static bool
save_param_ioctl_argp(void *data, long hlp, char *dest_buf, size_t dest_bs)
{
   const unsigned long request = (unsigned long)hlp;
   const unsigned n = ioctl_argp_save_size(request);

   if (!n || !data)
      return true;

   const unsigned to_copy = n < dest_bs ? n : (unsigned)dest_bs;

   if (copy_from_user(dest_buf, data, to_copy))
      memcpy(dest_buf, "<fault>", 8);

   return true;
}

const struct sys_param_type ptype_ioctl_argp = {
   .name      = "ioctl_argp",
   .slot_size = 64,
   .save      = save_param_ioctl_argp,
};

/* fcntl: only F_SETLK / F_SETLKW / F_GETLK take a struct flock*;
 * Tilck currently doesn't implement any of those (kernel rejects
 * them). For F_DUPFD / F_SETFD / F_SETFL / F_DUPFD_CLOEXEC the
 * arg is an immediate int — no copy needed; the userspace dump
 * renders it from the raw arg value. So today this save callback
 * has nothing to do, but the slot is reserved for the day flock
 * support lands. */
static bool
save_param_fcntl_arg(void *data, long hlp, char *dest_buf, size_t dest_bs)
{
   (void)data; (void)hlp; (void)dest_buf; (void)dest_bs;
   return true;
}

const struct sys_param_type ptype_fcntl_arg = {
   .name      = "fcntl_arg",
   .slot_size = 0,           /* no struct capture today */
   .save      = save_param_fcntl_arg,
};
