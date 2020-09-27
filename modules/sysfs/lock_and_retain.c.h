/* SPDX-License-Identifier: BSD-2-Clause */

static int
sysfs_retain_inode(struct fs *fs, vfs_inode_ptr_t inode)
{
   ASSERT(inode != NULL);
   return retain_obj((struct sysfs_inode *)inode);
}

static int
sysfs_release_inode(struct fs *fs, vfs_inode_ptr_t inode)
{
   ASSERT(inode != NULL);
   return release_obj((struct sysfs_inode *)inode);
}

static void
sysfs_exclusive_lock(struct fs *fs)
{
   struct sysfs_data *d = fs->device_data;
   rwlock_wp_exlock(&d->rwlock);
}

static void
sysfs_exclusive_unlock(struct fs *fs)
{
   struct sysfs_data *d = fs->device_data;
   rwlock_wp_exunlock(&d->rwlock);
}

static void
sysfs_shared_lock(struct fs *fs)
{
   struct sysfs_data *d = fs->device_data;
   rwlock_wp_shlock(&d->rwlock);
}

static void
sysfs_shared_unlock(struct fs *fs)
{
   struct sysfs_data *d = fs->device_data;
   rwlock_wp_shunlock(&d->rwlock);
}
