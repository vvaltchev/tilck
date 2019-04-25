/* SPDX-License-Identifier: BSD-2-Clause */

static const file_ops static_ops_ramfs =
{
   .read = ramfs_read,
   .write = ramfs_write,
   .seek = ramfs_seek,
   .ioctl = ramfs_ioctl,
   .fcntl = ramfs_fcntl,
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
   retain_obj(inode);

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

   if ((fl & O_DIRECTORY) && (i->type != VFS_DIR))
      return -ENOTDIR;

   if ((fl & O_CREAT) && (fl & O_EXCL))
      return -EEXIST;

   if ((fl & (O_WRONLY | O_RDWR)) && i->type == VFS_DIR)
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
ramfs_open(vfs_path *p, fs_handle *out, int fl, mode_t mod)
{
   ramfs_path *rp = (ramfs_path *) &p->fs_path;
   ramfs_data *d = p->fs->device_data;
   ramfs_inode *i = rp->inode;
   ramfs_inode *idir = rp->dir_inode;
   int rc;

   if (!i) {

      if (!(fl & O_CREAT))
         return -ENOENT;

      if (!(idir->mode & 0300)) /* write + execute */
         return -EACCES;

      if (!(i = ramfs_create_inode_file(d, mod, idir)))
         return -ENOSPC;

      if ((rc = ramfs_dir_add_entry(idir, p->last_comp, i))) {
         ramfs_destroy_inode(d, i);
         return rc;
      }

   } else {

      if (!(idir->mode & 0500)) /* read + execute */
         return -EACCES;

      if ((rc = ramfs_open_existing_checks(fl, i)))
         return rc;
   }

   return ramfs_open_int(p->fs, i, out, fl);
}
