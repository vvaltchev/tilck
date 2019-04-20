/* SPDX-License-Identifier: BSD-2-Clause */

static const file_ops static_ops_ramfs = {

   .read = ramfs_file_read,
   .write = ramfs_file_write,
   .seek = ramfs_file_seek,
   .ioctl = ramfs_file_ioctl,
   .fcntl = ramfs_file_fcntl,
   .stat = ramfs_stat64,
   .mmap = NULL,
   .munmap = NULL,
   .exlock = ramfs_file_exlock,
   .exunlock = ramfs_file_exunlock,
   .shlock = ramfs_file_shlock,
   .shunlock = ramfs_file_shunlock,
};

static int
ramfs_open_int(filesystem *fs, ramfs_inode *inode, fs_handle *out, int fl)
{
   ramfs_handle *h;

   if (!(h = kzmalloc(sizeof(ramfs_handle))))
      return -ENOMEM;

   h->inode = inode;
   h->fs = fs;
   h->fops = &static_ops_ramfs;

   if (fl & O_TRUNC) {
      rwlock_wp_exlock(&inode->rwlock);
      ramfs_inode_truncate(inode, 0);
      rwlock_wp_exunlock(&inode->rwlock);
   }

   *out = h;
   return 0;
}

static int ramfs_open_existing_checks(int fl, ramfs_inode *i)
{
   if (!(fl & O_WRONLY) && !(i->mode & 0400))
      return -EACCES;

   if ((fl & O_WRONLY) && !(i->mode & 0200))
      return -EACCES;

   if ((fl & O_RDWR) && !(i->mode & 0600))
      return -EACCES;

   if ((fl & O_DIRECTORY) && (i->type != RAMFS_DIRECTORY))
      return -ENOTDIR;

   if ((fl & O_CREAT) && (fl & O_EXCL))
      return -EEXIST;

   if ((fl & (O_WRONLY | O_RDWR)) && i->type == RAMFS_DIRECTORY)
      return -EISDIR;

   /*
    * On some systems O_TRUNC | O_RDONLY has undefined behavior and on some
    * the file might actually be truncated. On Tilck, that is simply NOT
    * allowed.
    */
   if ((fl & O_TRUNC) && !(fl & (O_WRONLY | O_RDWR)))
      return -EINVAL;

   return 0;
}

static int
ramfs_open(filesystem *fs, const char *path, fs_handle *out, int fl, mode_t mod)
{
   ramfs_data *d = fs->device_data;
   ramfs_inode *next_idir = NULL;
   ramfs_inode *idir = d->root;
   ramfs_inode *i = NULL;
   ramfs_entry *e;
   const char *pc;
   int rc;

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
      if ((e = ramfs_dir_get_entry_by_name(idir, pc, path - pc)))
         i = e->inode;
   }
   rwlock_wp_shunlock(&idir->rwlock);

   if (!i) {

      if (!(fl & O_CREAT))
         return -ENOENT;

      // TODO: do we have the permission to create a file in this directory?

      if (!(i = ramfs_create_inode_file(d, mod, idir)))
         return -ENOSPC;

      if ((rc = ramfs_dir_add_entry(idir, pc, i)))
         return rc;

   } else {

      if ((rc = ramfs_open_existing_checks(fl, i)))
         return rc;
   }

   return ramfs_open_int(fs, i, out, fl);
}
