/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/string_util.h>
#include <tilck/kernel/modules.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/fs/devfs.h>
#include <tilck/kernel/fs/vfs.h>
#include <linux/major.h>

#define DEV_MINOR_NULL 3
#define DEV_MINOR_ZERO 5
#define DEV_MINOR_FULL 7

static ssize_t null_read(fs_handle h, char *buf, size_t size, offt *pos)
{
   ASSERT(*pos == 0);
   return 0;
}

static ssize_t null_write(fs_handle h, char *buf, size_t size, offt *pos)
{
   ASSERT(*pos == 0);
   return size;
}

static offt null_seek(fs_handle h, offt off, int whence)
{
   struct devfs_handle *dh = h;
   dh->h_fpos = 0;
   return dh->h_fpos;
}

static const struct file_ops static_ops_null = {
   .read = null_read,
   .write = null_write,
   .seek = null_seek,
};

static ssize_t zero_read(fs_handle h, char *buf, size_t size, offt *pos)
{
   ASSERT(*pos == 0);
   bzero(buf, size);
   return size;
}

static const struct file_ops static_ops_zero = {
   .read = zero_read,
   .write = null_write,
   .seek = null_seek,
};

static ssize_t full_write(fs_handle h, char *buf, size_t size, offt *pos)
{
   return -ENOSPC;
}

static const struct file_ops static_ops_full = {
   .read = zero_read,
   .write = full_write,
   .seek = null_seek,
};

static int
null_create_device_file(int minor,
                        enum vfs_entry_type *type,
                        struct devfs_file_info *nfo)
{
   const struct file_ops *ops;
   switch (minor) {
      case DEV_MINOR_NULL:
         ops = &static_ops_null;
         break;
      case DEV_MINOR_ZERO:
         ops = &static_ops_zero;
         break;
      case DEV_MINOR_FULL:
         ops = &static_ops_full;
         break;
      default:
         return -EINVAL;
   }
   *type = VFS_CHAR_DEV;
   nfo->fops = ops;
   return 0;
}

static void init_null(void)
{
   int rc;
   struct driver_info *di = kzalloc_obj(struct driver_info);
   if (!di)
      panic("NULL: not enough memory for struct driver_info");

   di->name = "null";
   di->create_dev_file = null_create_device_file;
   register_driver(di, MEM_MAJOR);
   if ((rc = create_dev_file("null", di->major, DEV_MINOR_NULL, NULL) < 0))
      panic("CHAR: unable to create devfile /dev/null (error: %d)", rc);
   if ((rc = create_dev_file("zero", di->major, DEV_MINOR_ZERO, NULL) < 0))
      panic("CHAR: unable to create devfile /dev/zero (error: %d)", rc);
   if ((rc = create_dev_file("full", di->major, DEV_MINOR_FULL, NULL) < 0))
      panic("CHAR: unable to create devfile /dev/full (error: %d)", rc);
}

static struct module null_module = {
   .name = "null",
   .priority = MOD_null_prio,
   .init = &init_null,
};

REGISTER_MODULE(&null_module);
