/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/color_defs.h>

#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/fs/devfs.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/kb.h>
#include <tilck/kernel/cmdline.h>

#include "tty_int.h"

tty *ttys[MAX_TTYS + 1];
tty *__curr_tty;

static ssize_t tty_read(fs_handle h, char *buf, size_t size)
{
   devfs_file_handle *dh = h;
   devfs_file *df = dh->devfs_file_ptr;
   tty *t = df->dev_minor ? ttys[df->dev_minor] : get_curr_tty();

   return tty_read_int(t, dh, buf, size);
}

static ssize_t tty_write(fs_handle h, char *buf, size_t size)
{
   devfs_file_handle *dh = h;
   devfs_file *df = dh->devfs_file_ptr;
   tty *t = df->dev_minor ? ttys[df->dev_minor] : get_curr_tty();

   return tty_write_int(t, dh, buf, size);
}

static int tty_ioctl(fs_handle h, uptr request, void *argp)
{
   devfs_file_handle *dh = h;
   devfs_file *df = dh->devfs_file_ptr;
   tty *t = df->dev_minor ? ttys[df->dev_minor] : get_curr_tty();

   return tty_ioctl_int(t, dh, request, argp);
}

static int tty_fcntl(fs_handle h, int cmd, uptr arg)
{
   devfs_file_handle *dh = h;
   devfs_file *df = dh->devfs_file_ptr;
   tty *t = df->dev_minor ? ttys[df->dev_minor] : get_curr_tty();

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
   ttys[minor]->minor = minor;
   return ttys[minor];
}

int tty_get_curr_tty_num(void)
{
   return get_curr_tty()->minor;
}

void
internal_tty_create_devfile(const char *filename, int major, int minor)
{
   int rc = create_dev_file(filename, major, minor);

   if (rc != 0)
      panic("TTY: unable to create /dev/%s (error: %d)", filename, rc);
}

static term *
tty_allocate_and_init_new_term(void)
{
   term *new_term = allocate_new_term();

   if (!new_term)
      panic("TTY: no enough memory a new term instance");

   init_term(new_term,
             term_get_vi(ttys[1]->term_inst),
             term_get_rows(ttys[1]->term_inst),
             term_get_cols(ttys[1]->term_inst));

   return new_term;
}

static void internal_init_tty(int major, int minor)
{
   ASSERT(minor < (int)ARRAY_SIZE(ttys));
   ASSERT(!ttys[minor]);

   if (minor == 0) {

      /*
       * tty0 is special: not a real tty but a special file always pointing
       * to the current tty. Therefore, just create the dev file.
       */

      internal_tty_create_devfile("tty0", major, minor);
      return;
   }

   tty *const t = allocate_and_init_tty(minor);

   t->term_inst = (minor == 1)
                     ? get_curr_term()
                     : tty_allocate_and_init_new_term();

   snprintk(t->dev_filename, sizeof(t->dev_filename), "tty%d", minor);
   internal_tty_create_devfile(t->dev_filename, major, minor);

   tty_input_init(t);
   term_set_filter(t->term_inst, tty_term_write_filter, &t->filter_ctx);
}

void init_tty(void)
{
   driver_info *di = kzmalloc(sizeof(driver_info));

   if (!di)
      panic("TTY: no enough memory for driver_info");

   di->name = "tty";
   di->create_dev_file = tty_create_device_file;
   int major = register_driver(di, 4);

   for (int i = 0; i <= kopt_tty_count; i++) {
      internal_init_tty(major, i);
   }

   __curr_tty = ttys[1];
   init_tty_dev();

   if (kb_register_keypress_handler(&tty_keypress_handler) < 0)
      panic("TTY: unable to register keypress handler");
}
