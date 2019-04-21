/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/fs/devfs.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/process.h>

#include <linux/major.h> // system header

#include "tty_int.h"

static inline tty *get_curr_process_tty(void)
{
   return get_curr_task()->pi->proc_tty;
}

static ssize_t ttyaux_read(fs_handle h, char *buf, size_t size)
{
   return tty_read_int(get_curr_process_tty(), h, buf, size);
}

static ssize_t ttyaux_write(fs_handle h, char *buf, size_t size)
{
   return tty_write_int(get_curr_process_tty(), h, buf, size);
}

static int ttyaux_ioctl(fs_handle h, uptr request, void *argp)
{
   return tty_ioctl_int(get_curr_process_tty(), h, request, argp);
}

static int ttyaux_fcntl(fs_handle h, int cmd, int arg)
{
   return tty_fcntl_int(get_curr_process_tty(), h, cmd, arg);
}

static kcond *ttyaux_get_rready_cond(fs_handle h)
{
   return &get_curr_process_tty()->input_cond;
}

static bool ttyaux_read_ready(fs_handle h)
{
   return tty_read_ready_int(get_curr_process_tty(), h);
}

static int
ttyaux_create_device_file(int minor, const file_ops **fops, enum devfs_entry *t)
{
   static const file_ops static_ops_ttyaux = {

      .read = ttyaux_read,
      .write = ttyaux_write,
      .ioctl = ttyaux_ioctl,
      .fcntl = ttyaux_fcntl,
      .get_rready_cond = ttyaux_get_rready_cond,
      .read_ready = ttyaux_read_ready,

      /* the tty device-file requires NO locking */
      .exlock = vfs_file_nolock,
      .exunlock = vfs_file_nolock,
      .shlock = vfs_file_nolock,
      .shunlock = vfs_file_nolock,
   };

   *t = DEVFS_CHAR_DEVICE;
   *fops = &static_ops_ttyaux;
   return 0;
}

/*
 * Creates the special /dev/tty file which redirects the tty_* funcs to the
 * tty that was current when the process was created.
 */
void init_ttyaux(void)
{
   driver_info *di = kzmalloc(sizeof(driver_info));

   if (!di)
      panic("TTY: no enough memory for driver_info");

   di->name = "ttyaux";
   di->create_dev_file = ttyaux_create_device_file;
   register_driver(di, TTYAUX_MAJOR);

   tty_create_devfile_or_panic("tty", TTYAUX_MAJOR, 0);
   tty_create_devfile_or_panic("console", TTYAUX_MAJOR, 1);
}
