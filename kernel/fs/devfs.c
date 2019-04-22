/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/fs/devfs.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/list.h>
#include <tilck/kernel/datetime.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/rwlock.h>
#include <tilck/kernel/paging.h>

#include <dirent.h> // system header

static filesystem *devfs;

/*
 * Registered drivers.
 */
static driver_info *drivers[32];
static u32 drivers_count;

filesystem *get_devfs(void)
{
   return devfs;
}

driver_info *get_driver_info(u16 major)
{
   for (u32 i = 0; i < ARRAY_SIZE(drivers) && drivers[i]; i++) {
      if (drivers[i]->major == major)
         return drivers[i];
   }

   return NULL;
}

/*
 * Registers the driver described by 'info'.
 * Returns driver's major number.
 */
int register_driver(driver_info *info, int arg_major)
{
   u16 major;

   /* Be sure there's always enough space. */
   VERIFY(drivers_count < ARRAY_SIZE(drivers) - 1);

   if (arg_major < 0) {

      major = 0;

      for (u32 i = 0; i < ARRAY_SIZE(drivers) && drivers[i]; i++) {
         if (drivers[i]->major == major)
            major++;
      }

   } else {

      major = (u16) arg_major;

      if (get_driver_info(major))
         panic("Duplicate major number: %d", major);
   }

   info->major = major;
   drivers[drivers_count++] = info;
   return major;
}

typedef struct {

   /*
    * Yes, sub-directories are NOT supported by devfs. The whole filesystem is
    * just one flat directory.
    */
   list files_list;
   ino_t inode;

} devfs_directory;

typedef struct {

   devfs_directory root_dir;
   rwlock_wp rwlock;
   datetime_t wrt_time;
   ino_t next_inode;

} devfs_data;

static inline ino_t devfs_get_next_inode(devfs_data *d)
{
   return d->next_inode++;
}

int create_dev_file(const char *filename, u16 major, u16 minor)
{
   ASSERT(devfs != NULL);

   filesystem *fs = devfs;
   driver_info *dinfo = get_driver_info(major);

   if (!dinfo)
      return -EINVAL;

   devfs_data *d = fs->device_data;
   devfs_file *f = kmalloc(sizeof(devfs_file));

   if (!f)
      return -ENOMEM;

   f->inode = devfs_get_next_inode(d);
   list_node_init(&f->dir_node);
   f->name = filename;
   f->dev_major = major;
   f->dev_minor = minor;

   int res = dinfo->create_dev_file(minor, &f->fops, &f->type);

   if (res < 0) {
      kfree2(f, sizeof(devfs_file));
      return res;
   }

   list_add_tail(&d->root_dir.files_list, &f->dir_node);
   return 0;
}

static ssize_t devfs_dir_read(fs_handle h, char *buf, size_t len)
{
   return -EINVAL;
}

static ssize_t devfs_dir_write(fs_handle h, char *buf, size_t len)
{
   return -EINVAL;
}

static off_t devfs_dir_seek(fs_handle h, off_t offset, int whence)
{
   return -EINVAL;
}

static int devfs_dir_ioctl(fs_handle h, uptr request, void *arg)
{
   return -EINVAL;
}

static int devfs_dir_fcntl(fs_handle h, int cmd, int arg)
{
   return -EINVAL;
}

int devfs_stat64(fs_handle h, struct stat64 *statbuf)
{
   devfs_file_handle *dh = h;
   devfs_file *df = dh->devfs_file_ptr;
   devfs_data *ddata = dh->fs->device_data;

   bzero(statbuf, sizeof(struct stat64));

   statbuf->st_dev = dh->fs->device_id;

   if (dh->type == DEVFS_CHAR_DEVICE) {
      statbuf->st_mode = 0666 | S_IFCHR;
      statbuf->st_ino = df->inode;
   } else if (dh->type == DEVFS_DIRECTORY) {
      statbuf->st_mode = 0555 | S_IFDIR;
      statbuf->st_ino = ddata->root_dir.inode;
   } else {
      NOT_REACHED();
   }

   statbuf->st_nlink = 1;
   statbuf->st_uid = 0; /* root */
   statbuf->st_gid = 0; /* root */

   if (dh->type == DEVFS_CHAR_DEVICE)
      statbuf->st_rdev = (dev_t)(df->dev_major << 8 | df->dev_minor);

   statbuf->st_size = 0;
   statbuf->st_blksize = PAGE_SIZE;
   statbuf->st_blocks = 0;

   statbuf->st_ctim.tv_sec = datetime_to_timestamp(ddata->wrt_time);
   statbuf->st_mtim.tv_sec = datetime_to_timestamp(ddata->wrt_time);
   statbuf->st_atim = statbuf->st_mtim;

   return 0;
}

static const file_ops static_ops_devfs =
{
   .read = devfs_dir_read,
   .write = devfs_dir_write,
   .seek = devfs_dir_seek,
   .ioctl = devfs_dir_ioctl,
   .fcntl = devfs_dir_fcntl,
   .mmap = NULL,
   .munmap = NULL,
   .exlock = vfs_file_nolock,
   .exunlock = vfs_file_nolock,
   .shlock = vfs_file_nolock,
   .shunlock = vfs_file_nolock,
};

static int devfs_open_root_dir(filesystem *fs, fs_handle *out)
{
   devfs_file_handle *h;

   if (!(h = kzmalloc(sizeof(devfs_file_handle))))
      return -ENOMEM;

   h->type = DEVFS_DIRECTORY;
   h->fs = fs;
   h->fops = &static_ops_devfs;

   *out = h;
   return 0;
}

static int devfs_open_file(filesystem *fs, devfs_file *pos, fs_handle *out)
{
   devfs_file_handle *h;

   if (!(h = kzmalloc(sizeof(devfs_file_handle))))
      return -ENOMEM;

   if (!(h->read_buf = kzmalloc(DEVFS_READ_BS))) {
      kfree2(h, sizeof(devfs_file_handle));
      return -ENOMEM;
   }

   h->type = pos->type;
   h->devfs_file_ptr = pos;
   h->fs = fs;
   h->fops = pos->fops;

   *out = h;
   return 0;
}

static int
devfs_open(filesystem *fs, const char *path, fs_handle *out, int fl, mode_t mod)
{
   devfs_data *d = fs->device_data;
   devfs_file *pos;
   size_t pl;

   /*
    * Path is expected to be striped from the mountpoint prefix, but the '/'
    * is kept. In other words, /dev/tty is /tty here.
    */

   ASSERT(*path == '/');
   path++;

   if (!*path) {
      /* path was "/" */
      return devfs_open_root_dir(fs, out);
   }

   pl = strlen(path);

   if (path[pl - 1] == '/')
      pl--;

   /*
    * Linearly iterate our linked list: we do not expect any time soon devfs
    * to contain more than a few files.
    */

   list_for_each_ro(pos, &d->root_dir.files_list, dir_node) {

      if (!strncmp(pos->name, path, pl) && !pos->name[pl]) {

         if (path[pl] == '/')
            return -ENOTDIR;

         if ((fl & O_CREAT) && (fl & O_EXCL))
            return -EEXIST;

         return devfs_open_file(fs, pos, out);
      }
   }

   if (fl & O_CREAT)
      return -EROFS;

   return -ENOENT;
}

static void devfs_close(fs_handle h)
{
   devfs_file_handle *devh = h;
   kfree2(devh->read_buf, DEVFS_READ_BS);
   kfree2(devh->write_buf, DEVFS_WRITE_BS);
   kfree2(devh, sizeof(devfs_file_handle));
}

static int devfs_dup(fs_handle fsh, fs_handle *dup_h)
{
   devfs_file_handle *h = fsh;
   devfs_file_handle *h2;
   h2 = kzmalloc(sizeof(devfs_file_handle));

   if (!h2)
      return -ENOMEM;

   memcpy(h2, h, sizeof(devfs_file_handle));

   if (h->read_buf) {

      h2->read_buf = kmalloc(DEVFS_READ_BS);

      if (!h2->read_buf) {
         kfree2(h2, sizeof(devfs_file_handle));
         return -ENOMEM;
      }

      memcpy(h2->read_buf, h->read_buf, DEVFS_READ_BS);
   }

   if (h->write_buf) {

      h2->write_buf = kmalloc(DEVFS_WRITE_BS);

      if (!h2->write_buf) {
         kfree2(h->read_buf, DEVFS_READ_BS);
         kfree2(h2, sizeof(devfs_file_handle));
         return -ENOMEM;
      }
   }

   *dup_h = h2;
   return 0;
}

static void devfs_exclusive_lock(filesystem *fs)
{
   devfs_data *d = fs->device_data;
   rwlock_wp_exlock(&d->rwlock);
}

static void devfs_exclusive_unlock(filesystem *fs)
{
   devfs_data *d = fs->device_data;
   rwlock_wp_exunlock(&d->rwlock);
}

static void devfs_shared_lock(filesystem *fs)
{
   devfs_data *d = fs->device_data;
   rwlock_wp_shlock(&d->rwlock);
}

static void devfs_shared_unlock(filesystem *fs)
{
   devfs_data *d = fs->device_data;
   rwlock_wp_shunlock(&d->rwlock);
}

static int
devfs_getdents64(fs_handle h, struct linux_dirent64 *dirp, u32 buf_size)
{
   devfs_file_handle *dh = h;
   devfs_data *d = dh->fs->device_data;
   u32 offset = 0, curr_index = 0;
   struct linux_dirent64 ent;
   devfs_file *pos;

   if (dh->type != DEVFS_DIRECTORY)
      return -ENOTDIR;

   list_for_each_ro(pos, &d->root_dir.files_list, dir_node) {

      if (curr_index < dh->read_pos) {
         curr_index++;
         continue;
      }

      const char *const file_name = pos->name;
      const u32 fl = (u32)strlen(file_name);
      const u32 entry_size = fl + 1 + sizeof(struct linux_dirent64);

      if (offset + entry_size > buf_size) {

         if (!offset) {

            /*
            * We haven't "returned" any entries yet and the buffer is too small
            * for our first entry.
            */

            return -EINVAL;
         }

         /* We "returned" at least one entry */
         return (int)offset;
      }

      ent.d_ino = 0;
      ent.d_off = (s64)(offset + entry_size);
      ent.d_reclen = (u16)entry_size;
      ent.d_type = DT_UNKNOWN;

      if (dh->type == DEVFS_CHAR_DEVICE)
         ent.d_type = DT_CHR;

      struct linux_dirent64 *user_ent = (void *)((char *)dirp + offset);

      if (copy_to_user(user_ent, &ent, sizeof(ent)) < 0)
         return -EFAULT;

      if (copy_to_user(user_ent->d_name, file_name, fl + 1) < 0)
         return -EFAULT;

      offset = (u32) ent.d_off; /* s64 to u32 precision drop */
      curr_index++;
      dh->read_pos++;
   }

   return (int)offset;
}

static const fs_ops static_fsops_devfs =
{
   .open = devfs_open,
   .close = devfs_close,
   .dup = devfs_dup,
   .getdents64 = devfs_getdents64,
   .unlink = NULL,
   .fstat = devfs_stat64,

   .fs_exlock = devfs_exclusive_lock,
   .fs_exunlock = devfs_exclusive_unlock,
   .fs_shlock = devfs_shared_lock,
   .fs_shunlock = devfs_shared_unlock,
};

filesystem *create_devfs(void)
{
   filesystem *fs;
   devfs_data *d;

   /* Disallow multiple instances of devfs */
   ASSERT(devfs == NULL);

   if (!(fs = kzmalloc(sizeof(filesystem))))
      return NULL;

   if (!(d = kzmalloc(sizeof(devfs_data)))) {
      kfree2(fs, sizeof(filesystem));
      return NULL;
   }

   d->next_inode = 1;
   d->root_dir.inode = devfs_get_next_inode(d);
   list_init(&d->root_dir.files_list);
   rwlock_wp_init(&d->rwlock);
   read_system_clock_datetime(&d->wrt_time);

   fs->fs_type_name = "devfs";
   fs->device_id = vfs_get_new_device_id();
   fs->flags = VFS_FS_RW;
   fs->device_data = d;
   fs->fsops = &static_fsops_devfs;

   return fs;
}

void init_devfs(void)
{
   devfs = create_devfs();

   if (!devfs)
      panic("Unable to create devfs");

   int rc = mountpoint_add(devfs, "/dev/");

   if (rc != 0)
      panic("mountpoint_add() failed with error: %d", rc);
}
