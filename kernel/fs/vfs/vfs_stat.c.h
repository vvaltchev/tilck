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
      ret = fsops->stat(fs, fsops->get_inode(h), statbuf);
   }
   vfs_shunlock(h);
   return ret;
}

int vfs_stat64(const char *path, struct stat64 *statbuf, bool res_last_sl)
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

   /* See the comment in vfs.h about the "fs-lock" funcs */
   vfs_fs_exlock(fs);
   {
      rc = vfs_resolve(fs, fs_path, &p, res_last_sl);

      if (!rc)
         rc = p.fs_path.inode
            ? fs->fsops->stat(fs, p.fs_path.inode, statbuf)
            : -ENOENT;
   }
   vfs_fs_exunlock(fs);
   release_obj(fs);     /* it was retained by get_retained_fs_at() */
   return rc;
}
