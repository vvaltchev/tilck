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
   VFS_FS_PATH_FUNCS_COMMON_HEADER(path, false, res_last_sl)

   rc = p.fs_path.inode
      ? fs->fsops->stat(fs, p.fs_path.inode, statbuf)
      : -ENOENT;

   VFS_FS_PATH_FUNCS_COMMON_FOOTER(path, false, res_last_sl)
}
