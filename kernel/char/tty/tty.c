/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/color_defs.h>

#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/fs/devfs.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/kb.h>

#include "tty_int.h"

tty *ttys[MAX_TTYS + 1];
tty *__curr_tty;

static ssize_t tty_read(fs_handle h, char *buf, size_t size)
{
   devfs_file_handle *dh = h;
   devfs_file *df = dh->devfs_file_ptr;
   tty *t = ttys[df->dev_minor];

   return tty_read_int(t, dh, buf, size);
}

static ssize_t tty_write(fs_handle h, char *buf, size_t size)
{
   devfs_file_handle *dh = h;
   devfs_file *df = dh->devfs_file_ptr;
   tty *t = ttys[df->dev_minor];

   return tty_write_int(t, dh, buf, size);
}

static int tty_ioctl(fs_handle h, uptr request, void *argp)
{
   devfs_file_handle *dh = h;
   devfs_file *df = dh->devfs_file_ptr;
   tty *t = ttys[df->dev_minor];

   return tty_ioctl_int(t, dh, request, argp);
}

static int tty_fcntl(fs_handle h, int cmd, uptr arg)
{
   devfs_file_handle *dh = h;
   devfs_file *df = dh->devfs_file_ptr;
   tty *t = ttys[df->dev_minor];

   return tty_fcntl_int(t, dh, cmd, arg);
}

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

static void init_tty_struct(tty *t)
{
   t->filter_ctx.t = t;
   t->c_term = default_termios;
   t->kd_mode = KD_TEXT;
   t->curr_color = make_color(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR);
}

static tty *allocate_and_init_tty(int minor)
{
   ttys[minor] = kzmalloc(sizeof(tty));

   if (!ttys[minor]) {
      panic("TTY: no enough memory for TTY %d", minor);
   }

   init_tty_struct(ttys[minor]);
   return ttys[minor];
}

static void internal_init_tty(int minor)
{
   ASSERT(minor < (int)ARRAY_SIZE(ttys));
   ASSERT(!ttys[minor]);

   tty *const t = allocate_and_init_tty(minor);

   if (minor == 1) {

      t->term_inst = get_curr_term();

   } else {

      t->term_inst = allocate_new_term();

      if (!t->term_inst)
         panic("TTY: no enough memory a new term instance");

      init_term(t->term_inst,
                term_get_vi(ttys[1]->term_inst),
                term_get_rows(ttys[1]->term_inst),
                term_get_cols(ttys[1]->term_inst));
   }

   driver_info *di = kzmalloc(sizeof(driver_info));

   if (!di)
      panic("TTY: no enough memory for driver_info");

   di->name = "tty";
   di->create_dev_file = tty_create_device_file;
   int major = register_driver(di);
   snprintk(t->dev_filename, sizeof(t->dev_filename), "tty%d", minor);

   int rc = create_dev_file(t->dev_filename, major, minor);

   if (rc != 0)
      panic("TTY: unable to create /dev/%s (error: %d)", t->dev_filename, rc);

   tty_input_init(t);
   term_set_filter_func(t->term_inst,
                        tty_term_write_filter,
                        &t->filter_ctx);
}

void init_tty(void)
{
   internal_init_tty0();

   for (int i = 1; i <= MAX_TTYS; i++) {
      internal_init_tty(i);
   }

   __curr_tty = ttys[1];

   if (kb_register_keypress_handler(&tty_keypress_handler) < 0)
      panic("TTY: unable to register keypress handler");
}
