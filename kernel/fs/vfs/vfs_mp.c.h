/* SPDX-License-Identifier: BSD-2-Clause */

static struct kmutex mp_mutex = STATIC_KMUTEX_INIT(mp_mutex, 0);
static struct mountpoint mps2[MAX_MOUNTPOINTS];
static struct fs *mp_root;

int mp_init(struct fs *root_fs)
{
   /* do not support changing the root struct fs */
   NO_TEST_ASSERT(!mp_root);

#ifdef UNIT_TEST_ENVIRONMENT
   bzero(mps2, sizeof(mps2));
#endif

   mp_root = root_fs;
   retain_obj(mp_root);
   return 0;
}

struct fs *mp_get_root(void)
{
   ASSERT(mp_root != NULL);
   return mp_root;
}

struct fs *mp_get_at_nolock(struct fs *host_fs, vfs_inode_ptr_t inode)
{
   ASSERT(kmutex_is_curr_task_holding_lock(&mp_mutex));

   for (u32 i = 0; i < ARRAY_SIZE(mps2); i++)
      if (mps2[i].host_fs_inode == inode && mps2[i].host_fs == host_fs)
         return mps2[i].target_fs;

   return NULL;
}

struct fs *mp_get_retained_at(struct fs *host_fs, vfs_inode_ptr_t inode)
{
   struct fs *ret;
   kmutex_lock(&mp_mutex);
   {
      if ((ret = mp_get_at_nolock(host_fs, inode)))
         retain_obj(ret);
   }
   kmutex_unlock(&mp_mutex);
   return ret;
}

struct mountpoint *mp_get_retained_mp_of(struct fs *target_fs)
{
   uptr i;
   struct mountpoint *res = NULL;

   kmutex_lock(&mp_mutex);
   {
      for (i = 0; i < ARRAY_SIZE(mps2); i++)
         if (mps2[i].target_fs == target_fs)
            break;

      if (i < ARRAY_SIZE(mps2)) {
         res = &mps2[i];
         retain_obj(res);
      }
   }
   kmutex_unlock(&mp_mutex);
   return res;
}

int mp_add(struct fs *target_fs, const char *target_path)
{
   struct vfs_path p;
   int rc;
   u32 i;

   /*
    * We need to resolve target_path in order to get the host_fs and the
    * host_fs's inode.
    */

   if ((rc = vfs_resolve(target_path, &p, false, true)))
      return rc;

   if (p.fs_path.type != VFS_DIR) {
      vfs_fs_shunlock(p.fs);
      release_obj(p.fs);
      return -ENOTDIR;
   }

   vfs_retain_inode_at(&p);

   /*
    * Unlock the host fs but do *not* release its ref-count: each entry in the
    * `mps2` table retains its `host_fs`, its `host_fs_inode` and its
    * `target_fs`.
    */
   vfs_fs_shunlock(p.fs);
   kmutex_lock(&mp_mutex);

   /* we need to have the root struct fs set */
   ASSERT(mp_root != NULL);

   if (mp_get_at_nolock(p.fs, p.fs_path.inode)) {
      vfs_release_inode_at(&p);
      kmutex_unlock(&mp_mutex);
      return -EBUSY; /* the target path is already a mount-point */
   }

   for (i = 0; i < ARRAY_SIZE(mps2); i++) {
      if (mps2[i].target_fs == target_fs) {
         vfs_release_inode_at(&p);
         kmutex_unlock(&mp_mutex);
         return -EPERM; /* mounting multiple times a FS is NOT permitted */
      }
   }

   /* search for a free slot in the `mps2` table */
   for (i = 0; i < ARRAY_SIZE(mps2); i++)
      if (!mps2[i].host_fs)
         break;

   if (i < ARRAY_SIZE(mps2)) {

      /* we've found a free slot */

      mps2[i] = (struct mountpoint) {
         .host_fs = p.fs,
         .host_fs_inode = p.fs_path.inode,
         .target_fs = target_fs,
      };

      rc = 0;

      /* Now that we've succeeded, we must retain the target_fs as well */
      retain_obj(target_fs);

   } else {

      /* no free slot, sorry */
      rc = -EMFILE;
      vfs_release_inode_at(&p);
      release_obj(p.fs);
   }

   kmutex_unlock(&mp_mutex);
   return rc;
}

int mp_remove(const char *target_path)
{
   NOT_IMPLEMENTED();
}

