/* SPDX-License-Identifier: BSD-2-Clause */

#include "ramfs_int.h"
#include "getdents.c.h"
#include "locking.c.h"
#include "dir_entries.c.h"
#include "inodes.c.h"
#include "stat.c.h"
#include "rw_ops.c.h"
#include "open.c.h"

static int ramfs_dup(fs_handle h, fs_handle *dup_h)
{
   ramfs_handle *new_h = kmalloc(sizeof(ramfs_handle));

   if (!new_h)
      return -ENOMEM;

   memcpy(new_h, h, sizeof(ramfs_handle));
   *dup_h = new_h;
   return 0;
}

static void ramfs_close(fs_handle h)
{
   ramfs_handle *rh = h;
   kfree2(rh, sizeof(ramfs_handle));
}

void ramfs_destroy(filesystem *fs)
{
   ramfs_data *d = fs->device_data;

   if (d) {

      if (d->root) {
         ramfs_destroy_inode(d, d->root);
      }

      rwlock_wp_destroy(&d->rwlock);
      kfree2(d, sizeof(ramfs_data));
   }

   kfree2(fs, sizeof(filesystem));
}

static const fs_ops static_fsops_ramfs = {

   .open = ramfs_open,
   .close = ramfs_close,
   .dup = ramfs_dup,
   .getdents64 = ramfs_getdents64,

   .fs_exlock = ramfs_exlock,
   .fs_exunlock = ramfs_exunlock,
   .fs_shlock = ramfs_shlock,
   .fs_shunlock = ramfs_shunlock,
};

filesystem *ramfs_create(void)
{
   filesystem *fs;
   ramfs_data *d;

   if (!(fs = kzmalloc(sizeof(filesystem))))
      return NULL;

   if (!(d = kzmalloc(sizeof(ramfs_data)))) {
      ramfs_destroy(fs);
      return NULL;
   }

   fs->device_data = d;
   rwlock_wp_init(&d->rwlock);
   d->next_inode_num = 1;
   d->root = ramfs_create_inode_dir(d, 0777, NULL);

   if (!d->root) {
      ramfs_destroy(fs);
      return NULL;
   }

   fs->fs_type_name = "ramfs";
   fs->device_id = vfs_get_new_device_id();
   fs->flags = VFS_FS_RW;
   fs->fsops = &static_fsops_ramfs;

   //tmp
   {
      ramfs_inode *i1 = ramfs_create_inode_dir(d, 0777, d->root);
      VERIFY(ramfs_dir_add_entry(d->root, "e1", i1) == 0);

      ramfs_inode *i2 = ramfs_create_inode_file(d, 0644, d->root);
      VERIFY(ramfs_dir_add_entry(d->root, "e2", i2) == 0);

      ramfs_inode *i11 = ramfs_create_inode_dir(d, 0777, i1);
      VERIFY(ramfs_dir_add_entry(i1, "e11", i11) == 0);

      ramfs_inode *i12 = ramfs_create_inode_dir(d, 0777, i1);
      VERIFY(ramfs_dir_add_entry(i1, "e12", i12) == 0);

      ramfs_inode *i111 = ramfs_create_inode_file(d, 0644, i11);
      VERIFY(ramfs_dir_add_entry(i11, "e111", i111) == 0);
   }
   //end tmp
   return fs;
}

