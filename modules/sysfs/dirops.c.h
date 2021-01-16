/* SPDX-License-Identifier: BSD-2-Clause */

static ssize_t
sysfs_dir_read(fs_handle h, char *buf, size_t len)
{
   return -EISDIR;
}

static ssize_t
sysfs_dir_write(fs_handle h, char *buf, size_t len)
{
   return -EISDIR;
}

static int
sysfs_dir_ioctl(fs_handle h, ulong request, void *arg)
{
   return -EINVAL;
}

static offt
sysfs_dir_seek(fs_handle h, offt target_off, int whence)
{
   struct sysfs_handle *sh = h;
   struct sysfs_entry *pos;
   offt off = 0;

   if (target_off < 0 || whence != SEEK_SET)
      return -EINVAL;

   list_for_each_ro(pos, &sh->inode->dir.entries_list, lnode) {

      if (off == target_off)
         break;

      off++;
   }

   if (off == target_off) {
      sh->dir.dpos = pos;
      sh->pos = off;
      return sh->pos;
   }

   return -EINVAL;
}

static const struct file_ops static_ops_dir_sysfs =
{
   .read = sysfs_dir_read,
   .write = sysfs_dir_write,
   .seek = sysfs_dir_seek,
   .ioctl = sysfs_dir_ioctl,
   .mmap = NULL,
   .munmap = NULL,
};

static int
sysfs_open_dir(struct fs *fs, struct sysfs_inode *pos, fs_handle *out)
{
   struct sysfs_handle *h;

   if (!(h = vfs_create_new_handle(fs, &static_ops_dir_sysfs)))
      return -ENOMEM;

   h->type = VFS_DIR;
   h->dir.dpos = list_first_obj(&pos->dir.entries_list,
                                struct sysfs_entry,
                                lnode);
   h->inode = pos;
   retain_obj(pos);

   *out = h;
   return 0;
}
