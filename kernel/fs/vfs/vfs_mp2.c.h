/* SPDX-License-Identifier: BSD-2-Clause */

static kmutex mp2_mutex = STATIC_KMUTEX_INIT(mp2_mutex, 0);
static mountpoint2 mps2[MAX_MOUNTPOINTS];
static filesystem *mp2_root;

int mp2_init(filesystem *root_fs)
{
   /* do not support changing the root filesystem */
   NO_TEST_ASSERT(!mp2_root);

   mp2_root = root_fs;

#ifdef UNIT_TEST_ENVIRONMENT
   bzero(mps2, sizeof(mps2));
#endif

   return 0;
}

filesystem *mp2_get_root(void)
{
   ASSERT(mp2_root != NULL);
   return mp2_root;
}

filesystem *mp2_get_at_nolock(filesystem *host_fs, vfs_inode_ptr_t inode)
{
   ASSERT(kmutex_is_curr_task_holding_lock(&mp2_mutex));

   for (u32 i = 0; i < ARRAY_SIZE(mps2); i++)
      if (mps2[i].host_fs_inode == inode && mps2[i].host_fs == host_fs)
         return mps2[i].target_fs;

   return NULL;
}

filesystem *mp2_get_retained_at(filesystem *host_fs, vfs_inode_ptr_t inode)
{
   filesystem *ret;
   kmutex_lock(&mp2_mutex);
   {
      if ((ret = mp2_get_at_nolock(host_fs, inode)))
         retain_obj(ret);
   }
   kmutex_unlock(&mp2_mutex);
   return ret;
}

int mp2_get_mountpoint_of(filesystem *target_fs, mountpoint2 *mp)
{
   uptr i;
   int rc = -ENOENT;

   kmutex_lock(&mp2_mutex);
   {
      for (i = 0; i < ARRAY_SIZE(mps2); i++)
         if (mps2[i].target_fs == target_fs)
            break;

      if (i < ARRAY_SIZE(mps2)) {
         *mp = mps2[i];
         rc = 0;
      }
   }
   kmutex_unlock(&mp2_mutex);
   return rc;
}

int mp2_add(filesystem *target_fs, const char *target_path)
{
   vfs_path p;
   int rc;
   u32 i;

   /*
    * We need to resolve target_path in order to get the host_fs and the
    * host_fs's inode.
    */

   if ((rc = vfs_resolve(target_path, &p, NULL, false, true)))
      return rc;

   if (p.fs_path.type != VFS_DIR) {
      vfs_fs_shunlock(p.fs);
      release_obj(p.fs);
      return -ENOTDIR;
   }

   p.fs->fsops->retain_inode(p.fs, p.fs_path.inode);

   /*
    * Unlock the host fs but do *not* release its ref-count: each entry in the
    * `mps2` table retains its `host_fs` and `host_fs_inode` objects.
    */
   vfs_fs_shunlock(p.fs);
   kmutex_lock(&mp2_mutex);

   /* we need to have the root filesystem set */
   ASSERT(mp2_root != NULL);

   if (mp2_get_at_nolock(p.fs, p.fs_path.inode)) {
      p.fs->fsops->release_inode(p.fs, p.fs_path.inode);
      kmutex_unlock(&mp2_mutex);
      return -EBUSY; /* the target path is already a mount-point */
   }

   for (i = 0; i < ARRAY_SIZE(mps2); i++) {
      if (mps2[i].target_fs == target_fs) {
         p.fs->fsops->release_inode(p.fs, p.fs_path.inode);
         kmutex_unlock(&mp2_mutex);
         return -EPERM; /* mounting multiple times a FS is NOT permitted */
      }
   }

   /* search for a free slot in the `mps2` table */
   for (i = 0; i < ARRAY_SIZE(mps2); i++)
      if (!mps2[i].host_fs)
         break;

   if (i < ARRAY_SIZE(mps2)) {

      /* we've found a free slot */

      mps2[i] = (mountpoint2) {
         .host_fs = p.fs,
         .host_fs_inode = p.fs_path.inode,
         .target_fs = target_fs,
      };

      rc = 0;

   } else {

      /* no free slot, sorry */
      rc = -EMFILE;
      p.fs->fsops->release_inode(p.fs, p.fs_path.inode);
      release_obj(p.fs);
   }

   kmutex_unlock(&mp2_mutex);
   return rc;
}

int mp2_remove(const char *target_path)
{
   NOT_IMPLEMENTED();
}

