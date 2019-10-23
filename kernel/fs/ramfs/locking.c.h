/* SPDX-License-Identifier: BSD-2-Clause */

static void ramfs_file_exlock(fs_handle h)
{
   struct ramfs_handle *rh = h;
   rwlock_wp_exlock(&rh->inode->rwlock);
}

static void ramfs_file_exunlock(fs_handle h)
{
   struct ramfs_handle *rh = h;
   rwlock_wp_exunlock(&rh->inode->rwlock);
}

static void ramfs_file_shlock(fs_handle h)
{
   struct ramfs_handle *rh = h;
   rwlock_wp_shlock(&rh->inode->rwlock);
}

static void ramfs_file_shunlock(fs_handle h)
{
   struct ramfs_handle *rh = h;
   rwlock_wp_shunlock(&rh->inode->rwlock);
}


static void ramfs_exlock(struct fs *fs)
{
   struct ramfs_data *d = fs->device_data;
   rwlock_wp_exlock(&d->rwlock);
}

static void ramfs_exunlock(struct fs *fs)
{
   struct ramfs_data *d = fs->device_data;
   rwlock_wp_exunlock(&d->rwlock);
}

static void ramfs_shlock(struct fs *fs)
{
   struct ramfs_data *d = fs->device_data;
   rwlock_wp_shlock(&d->rwlock);
}

static void ramfs_shunlock(struct fs *fs)
{
   struct ramfs_data *d = fs->device_data;
   rwlock_wp_shunlock(&d->rwlock);
}
