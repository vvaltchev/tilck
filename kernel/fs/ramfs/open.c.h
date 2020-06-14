/* SPDX-License-Identifier: BSD-2-Clause */

static const struct file_ops static_ops_ramfs =
{
   .read = ramfs_read,
   .write = ramfs_write,
   .readv = ramfs_readv,
   .writev = ramfs_writev,
   .seek = ramfs_seek,
   .ioctl = ramfs_ioctl,
   .mmap = ramfs_mmap,
   .munmap = ramfs_munmap,
   .handle_fault = ramfs_handle_fault,
};

static int
ramfs_open_int(struct fs *fs, struct ramfs_inode *inode, fs_handle *out, int fl)
{
   struct ramfs_handle *h;

   if (!(h = kzmalloc(sizeof(struct ramfs_handle))))
      return -ENOMEM;

   vfs_init_fs_handle_base_fields((void *)h, fs, &static_ops_ramfs);
   h->inode = inode;
   h->spec_flags = VFS_SPFL_MMAP_SUPPORTED;
   retain_obj(inode);

   if (inode->type == VFS_DIR) {

      /*
       * If we're opening a directory, register its handle in inode's handles
       * list so that if unlink() is called on an entry E and there are open
       * handles to E's parent-dir where h->dpos == E, their dpos is moved
       * forward. This is a VERY CORNER CASE, but it *MUST BE* handled.
       */
      list_node_init(&h->node);
      list_add_tail(&inode->handles_list, &h->node);
      h->dpos = list_first_obj(&inode->entries_list, struct ramfs_entry, lnode);

   } else {

      if (fl & O_TRUNC) {

         DEBUG_ONLY_UNSAFE(int rc =)
            ramfs_inode_truncate_safe(inode, 0, false);

         ASSERT(rc == 0);
      }
   }

   *out = h;
   return 0;
}

static int ramfs_open_existing_checks(int fl, struct ramfs_inode *i)
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
ramfs_open(struct vfs_path *p, fs_handle *out, int fl, mode_t mod)
{
   struct ramfs_path *rp = (struct ramfs_path *) &p->fs_path;
   struct ramfs_data *d = p->fs->device_data;
   struct ramfs_inode *i = rp->inode;
   struct ramfs_inode *idir = rp->dir_inode;
   struct locked_file *lf = NULL;
   int rc;

   if (!i) {

      if (!(fl & O_CREAT))
         return -ENOENT;

      if (!(idir->mode & 0300)) /* write + execute */
         return -EACCES;

      if (!(i = ramfs_create_inode_file(d, mod, idir)))
         return -ENOSPC;

      rc = acquire_subsystem_file_exlock(p->fs,
                                         i,
                                         SUBSYS_VFS,
                                         &lf);

      if (rc) {
         ramfs_destroy_inode(d, i);
         return rc;
      }

      if ((rc = ramfs_dir_add_entry(idir, p->last_comp, i))) {
         ramfs_destroy_inode(d, i);
         return rc;
      }

   } else {

      if (!(idir->mode & 0500)) /* read + execute */
         return -EACCES;

      if ((rc = ramfs_open_existing_checks(fl, i)))
         return rc;

      if (i->type == VFS_FILE && (fl & (O_WRONLY | O_RDWR))) {

         rc = acquire_subsystem_file_exlock(p->fs,
                                            i,
                                            SUBSYS_VFS,
                                            &lf);

         if (rc)
            return rc;
      }
   }

   rc = ramfs_open_int(p->fs, i, out, fl);

   if (LIKELY(!rc)) {

      /* Success: now set the `lf` field in the file handle */
      ((struct fs_handle_base *) *out)->lf = lf;

   } else {

      /* Open failed: we have to release the lock obj (if any) */

      if (lf)
         release_subsystem_file_exlock(lf);
   }

   return rc;
}
