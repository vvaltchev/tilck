/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/fs/devfs.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/process.h>

#include <linux/major.h> // system header

#include "tty_int.h"

enum term_type get_curr_proc_tty_term_type(void)
{
   struct tty *t = get_curr_task()->pi->proc_tty;
   return t->tparams.type;
}

struct tty *get_curr_process_tty(void)
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

static struct kcond *ttyaux_get_rready_cond(fs_handle h)
{
   return &get_curr_process_tty()->input_cond;
}

static int ttyaux_read_ready(fs_handle h)
{
   return tty_read_ready_int(get_curr_process_tty(), h);
}

static int
ttyaux_create_device_file(int minor,
                          const struct file_ops **fops,
                          enum vfs_entry_type *t,
                          int *spec_flags_ref)
{
   static const struct file_ops static_ops_ttyaux = {

      .read = ttyaux_read,
      .write = ttyaux_write,
      .ioctl = ttyaux_ioctl,
      .get_rready_cond = ttyaux_get_rready_cond,
      .read_ready = ttyaux_read_ready,
   };

   *t = VFS_CHAR_DEV;
   *fops = &static_ops_ttyaux;
   return 0;
}

/*
 * Creates the special /dev/tty file which redirects the tty_* funcs to the
 * tty that was current when the process was created.
 */
void init_ttyaux(void)
{
   struct driver_info *di = kzmalloc(sizeof(struct driver_info));

   if (!di)
      panic("TTY: no enough memory for struct driver_info");

   di->name = "ttyaux";
   di->create_dev_file = ttyaux_create_device_file;
   register_driver(di, TTYAUX_MAJOR);

   tty_create_devfile_or_panic("tty", TTYAUX_MAJOR, 0);
   tty_create_devfile_or_panic("console", TTYAUX_MAJOR, 1);
}
