/* SPDX-License-Identifier: BSD-2-Clause */

int vfs_fstat64(fs_handle h, struct stat64 *statbuf)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(h != NULL);

   fs_handle_base *hb = (fs_handle_base *) h;
   int ret;

   vfs_shlock(h);
   {
      ret = hb->fs->fsops->fstat(h, statbuf);
   }
   vfs_shunlock(h);
   return ret;
}

int vfs_stat64(const char *path, struct stat64 *statbuf)
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
