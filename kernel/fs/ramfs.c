/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/list.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/rwlock.h>
#include <tilck/kernel/datetime.h>

#include <dirent.h> // system header

enum ramfs_entry {
   RAMFS_FILE,
   RAMFS_DIRECTORY,
   RAMFS_SYMLINK,
};

typedef struct {

   /* fs_handle_base */
   FS_HANDLE_BASE_FIELDS

   /* ramfs-specific fields */
   enum ramfs_entry type;

   u32 read_pos;
   u32 write_pos;

} ramfs_handle;

typedef struct {

   rwlock_wp rwlock;

} ramfs_data;

static void ramfs_exclusive_lock(filesystem *fs)
{
   ramfs_data *d = fs->device_data;
   rwlock_wp_exlock(&d->rwlock);
}

static void ramfs_exclusive_unlock(filesystem *fs)
{
   ramfs_data *d = fs->device_data;
   rwlock_wp_exunlock(&d->rwlock);
}

static void ramfs_shared_lock(filesystem *fs)
{
   ramfs_data *d = fs->device_data;
   rwlock_wp_shlock(&d->rwlock);
}

static void ramfs_shared_unlock(filesystem *fs)
{
   ramfs_data *d = fs->device_data;
   rwlock_wp_shunlock(&d->rwlock);
}

static int
ramfs_getdents64(fs_handle h, struct linux_dirent64 *dirp, u32 buf_size)
{
   ramfs_handle *dh = h;
   ramfs_data *d = dh->fs->device_data;
   (void)d;
   return 0;
}

filesystem *ramfs_create(void)
{
   filesystem *fs;
   ramfs_data *d;

   if (!(fs = kzmalloc(sizeof(filesystem))))
      return NULL;

   if (!(d = kzmalloc(sizeof(ramfs_data)))) {
      kfree2(fs, sizeof(filesystem));
      return NULL;
   }

   fs->fs_type_name = "ramfs";
   fs->device_id = vfs_get_new_device_id();
   fs->flags = VFS_FS_RW;
   fs->device_data = d;

   fs->open = NULL;
   fs->close = NULL;
   fs->dup = NULL;
   fs->getdents64 = ramfs_getdents64;

   fs->fs_exlock = ramfs_exclusive_lock;
   fs->fs_exunlock = ramfs_exclusive_unlock;
   fs->fs_shlock = ramfs_shared_lock;
   fs->fs_shunlock = ramfs_shared_unlock;

   rwlock_wp_init(&d->rwlock);
   return fs;
}

void ramfs_destroy(filesystem *fs)
{
   ramfs_data *d = fs->device_data;

   rwlock_wp_destroy(&d->rwlock);

   kfree2(d, sizeof(ramfs_data));
   kfree2(fs, sizeof(filesystem));
}
