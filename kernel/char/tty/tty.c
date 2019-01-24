/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/color_defs.h>

#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/fs/devfs.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/term.h>

#include <termios.h>      // system header
#include <linux/kd.h>     // system header

#include "term_int.h"
#include "tty_int.h"

tty *ttys[MAX_TTYS];
tty *__curr_tty;

/* tty input */
tty_ctrl_sig_func tty_special_ctrl_handlers[256];

/* tty ioctl */
struct termios c_term;
u32 tty_kd_mode = KD_TEXT;

/* tty output */
u16 tty_saved_cursor_row;
u16 tty_saved_cursor_col;
term_write_filter_ctx_t term_write_filter_ctx;

/* other (misc) */
u8 tty_curr_color = make_color(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR);

ssize_t tty_write(fs_handle h, char *buf, size_t size)
{
   devfs_file_handle *dh = h;
   devfs_file *df = dh->devfs_file_ptr;
   tty *t = ttys[df->dev_minor];
   (void)t;

   /* term_write's size is limited to 2^20 - 1 */
   size = MIN(size, (size_t)MB - 1);
   term_write(get_curr_term(), buf, size, tty_curr_color);
   return size;
}

/* ----------------- Driver interface ---------------- */

int tty_ioctl(fs_handle h, uptr request, void *argp);
int tty_fcntl(fs_handle h, int cmd, uptr arg);
ssize_t tty_read(fs_handle h, char *buf, size_t size);
ssize_t tty_write(fs_handle h, char *buf, size_t size);

/* ---- */

static int
tty_create_device_file(int minor, file_ops *ops, devfs_entry_type *t)
{
   *t = DEVFS_CHAR_DEVICE;

   bzero(ops, sizeof(file_ops));

   ops->read = tty_read;
   ops->write = tty_write;
   ops->ioctl = tty_ioctl;
   ops->fcntl = tty_fcntl;

   /* the tty device-file requires NO locking */
   ops->exlock = &vfs_file_nolock;
   ops->exunlock = &vfs_file_nolock;
   ops->shlock = &vfs_file_nolock;
   ops->shunlock = &vfs_file_nolock;
   return 0;
}

static void internal_init_tty(int minor)
{
   ASSERT(minor < (int)ARRAY_SIZE(ttys));
   ASSERT(!ttys[minor]);

   ttys[minor] = kzmalloc(sizeof(tty));

   if (!ttys[minor]) {
      panic("TTY: no enough memory for TTY %d", minor);
   }

   tty *t = ttys[minor];

   c_term = default_termios;
   driver_info *di = kmalloc(sizeof(driver_info));

   if (!di)
      panic("TTY: no enough memory for init_tty()");

   di->name = "tty";
   di->create_dev_file = tty_create_device_file;
   int major = register_driver(di);
   int rc = create_dev_file("tty", major, minor);

   if (rc != 0)
      panic("TTY: unable to create /dev/tty (error: %d)", rc);

   tty_input_init(t);
   term_write_filter_ctx.t = t;
   term_set_filter_func(get_curr_term(),
                        tty_term_write_filter,
                        &term_write_filter_ctx);
}

void init_tty(void)
{
   for (int i = 0; i < 1; i++) {
      internal_init_tty(i);
   }

   __curr_tty = ttys[0];
}
