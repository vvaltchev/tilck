/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/fs/devfs.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/kmalloc.h>

#include "tty_int.h"

static ssize_t tty0_read(fs_handle h, char *buf, size_t size)
{
   return tty_read_int(get_curr_tty(), h, buf, size);
}

static ssize_t tty0_write(fs_handle h, char *buf, size_t size)
{
   return tty_write_int(get_curr_tty(), h, buf, size);
}

static int tty0_ioctl(fs_handle h, uptr request, void *argp)
{
   return tty_ioctl_int(get_curr_tty(), h, request, argp);
}

static int tty0_fcntl(fs_handle h, int cmd, uptr arg)
{
   return tty_fcntl_int(get_curr_tty(), h, cmd, arg);
}

static int
tty_create_tty0_device_file(int minor, file_ops *ops, devfs_entry_type *t)
{
   *t = DEVFS_CHAR_DEVICE;

   bzero(ops, sizeof(file_ops));

   ops->read = tty0_read;
   ops->write = tty0_write;
   ops->ioctl = tty0_ioctl;
   ops->fcntl = tty0_fcntl;

   /* NO locking */
   ops->exlock = &vfs_file_nolock;
   ops->exunlock = &vfs_file_nolock;
   ops->shlock = &vfs_file_nolock;
   ops->shunlock = &vfs_file_nolock;
   return 0;
}

/* /dev/tty0 is special: it always "points" to the current tty */
void internal_init_tty0(void)
{
   driver_info *di = kzmalloc(sizeof(driver_info));

   if (!di)
      panic("TTY: no enough memory for driver_info");

   di->name = "tty";
   di->create_dev_file = tty_create_tty0_device_file;
   int major = register_driver(di);

   int rc = create_dev_file("tty0", major, 0);

   if (rc != 0)
      panic("TTY: unable to create /dev/tty0 (error: %d)", rc);
}
