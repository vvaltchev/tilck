/* SPDX-License-Identifier: BSD-2-Clause */

typedef struct {

   filesystem *host_fs;
   vfs_inode_ptr_t host_fs_inode;
   filesystem *target_fs;

} mountpoint2;

static kmutex mp2_mutex = STATIC_KMUTEX_INIT(mp2_mutex, 0);
static mountpoint2 mps[MAX_MOUNTPOINTS];
static filesystem *mp2_root;

int mp2_init(filesystem *root_fs)
{
   /* do not support changing the root filesystem */
   ASSERT(!mp2_root);

   (void)mps;

   mp2_root = root_fs;
   return 0;
}

int mp2_add(filesystem *fs, const char *target_path)
{
   kmutex_lock(&mp2_mutex);

   /* we need to have the root filesystem set */
   ASSERT(mp2_root != NULL);

   kmutex_unlock(&mp2_mutex);
   return 0;
}

int mp2_remove(const char *target_path)
{
   kmutex_lock(&mp2_mutex);

   /* we need to have the root filesystem set */
   ASSERT(mp2_root != NULL);

   kmutex_unlock(&mp2_mutex);
   return 0;
}

