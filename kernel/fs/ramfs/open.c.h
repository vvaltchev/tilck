/* SPDX-License-Identifier: BSD-2-Clause */

static ssize_t ramfs_dir_read(fs_handle h, char *buf, size_t len)
{
   return -EINVAL;
}

static ssize_t ramfs_dir_write(fs_handle h, char *buf, size_t len)
{
   return -EINVAL;
}

static off_t ramfs_dir_seek(fs_handle h, off_t offset, int whence)
{
   return -EINVAL;
}

static int ramfs_dir_ioctl(fs_handle h, uptr request, void *arg)
{
   return -EINVAL;
}

static int ramfs_open_int(filesystem *fs, ramfs_inode *inode, fs_handle *out)
{
   ramfs_handle *h;

   if (!(h = kzmalloc(sizeof(ramfs_handle))))
      return -ENOMEM;

   h->inode = inode;
   h->fs = fs;
   h->fops = (file_ops) {
      .read = NULL,
      .write = NULL,
      .seek = NULL,
      .ioctl = NULL,
      .stat = ramfs_stat64,
      .exlock = ramfs_file_exlock,
      .exunlock = ramfs_file_exunlock,
      .shlock = ramfs_file_shlock,
      .shunlock = ramfs_file_shunlock,
   };

   *out = h;
   return 0;
}

static int
ramfs_open(filesystem *fs, const char *path, fs_handle *out, int fl, mode_t mod)
{
   ramfs_data *d = fs->device_data;
   ramfs_inode *idir = d->root;
   ramfs_inode *next_idir = NULL;
   ramfs_entry *e;
   ramfs_inode *i;
   const char *pc;
   /*
    * Path is expected to be striped from the mountpoint prefix, but the '/'
    * is kept. For example, if ramfs is mounted at /tmp, and the file /tmp/x
    * is referred, here will get path == "/x".
    */

   ASSERT(*path == '/');
   path++;
   pc = path;

   while (*path) {

      if (*path != '/') {
         path++;
         continue;
      }

      /*
       * We hit a slash '/' in the path: we now must lookup this path component.
       *
       * NOTE: the code in upper layers normalizes the user paths, but it makes
       * sense to ASSERT that.
       */

      ASSERT(path[1] != '/');

      rwlock_wp_shlock(&idir->rwlock);
      {
         if (!(e = ramfs_dir_get_entry_by_name(idir, pc, path - pc))) {
            rwlock_wp_shunlock(&idir->rwlock);
            return -ENOENT;
         }

         if (!path[1]) {
            /* path's last character was a slash */
            if (e->inode->type != RAMFS_DIRECTORY) {
               rwlock_wp_shunlock(&idir->rwlock);
               return -ENOTDIR;
            }
         }

         next_idir = e->inode;
      }
      rwlock_wp_shunlock(&idir->rwlock);
      idir = next_idir;

      path++;
      pc = path;
   }

   rwlock_wp_shlock(&idir->rwlock);
   {
      if (!(e = ramfs_dir_get_entry_by_name(idir, pc, path - pc))) {
         rwlock_wp_shunlock(&idir->rwlock);
         return -ENOENT;
      }

      i = e->inode;
   }
   rwlock_wp_shunlock(&idir->rwlock);

   return ramfs_open_int(fs, i, out);
}
