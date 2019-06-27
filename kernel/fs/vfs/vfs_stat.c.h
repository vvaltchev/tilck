/* SPDX-License-Identifier: BSD-2-Clause */

int vfs_fstat64(fs_handle h, struct stat64 *statbuf)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(h != NULL);

   fs_handle_base *hb = (fs_handle_base *) h;
   filesystem *fs = hb->fs;
   const fs_ops *fsops = fs->fsops;
   int ret;

   vfs_shlock(h);
   {
      if (fsops->new_stat) {
         ret = fsops->new_stat(fs, fsops->get_inode(h), statbuf);
      } else {
         printk("[fstat] Use the *old* fstat impl for %s\n", fs->fs_type_name);
         ret = fsops->fstat(h, statbuf);
      }
   }
   vfs_shunlock(h);
   return ret;
}

static int old_vfs_stat64(const char *path, struct stat64 *statbuf)
{
   fs_handle h = NULL;
   int rc;

   if ((rc = vfs_open(path, &h, O_RDONLY, 0)) < 0)
      return rc;

   /* If vfs_open() succeeded, `h` must be != NULL */
   ASSERT(h != NULL);

   rc = vfs_fstat64(h, statbuf);
   vfs_close(h);
   return 0;
}

int vfs_stat64(const char *path, struct stat64 *statbuf)
{
   const char *fs_path;
   filesystem *fs;
   vfs_path p;
   int rc;

   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(path != NULL);
   ASSERT(*path == '/'); /* VFS works only with absolute paths */

   if (!(fs = get_retained_fs_at(path, &fs_path)))
      return -ENOENT;

   if (!fs->fsops->new_stat) {
      printk("using the old stat for fs: %s\n", fs->fs_type_name);
      release_obj(fs);
      return old_vfs_stat64(path, statbuf);
   }

   /* See the comment in vfs.h about the "fs-lock" funcs */
   vfs_fs_exlock(fs);
   {
      rc = vfs_resolve(fs, fs_path, &p);

      if (!rc)
         rc = p.fs_path.inode
            ? fs->fsops->new_stat(fs, p.fs_path.inode, statbuf)
            : -ENOENT;
   }
   vfs_fs_exunlock(fs);
   release_obj(fs);     /* it was retained by get_retained_fs_at() */
   return rc;
}
