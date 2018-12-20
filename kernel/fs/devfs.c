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

#include <dirent.h> // system header

static filesystem *devfs;

/*
 * Registered drivers. The major number is just an index in this array.
 */
static driver_info *drivers[16];
static u32 drivers_count;

filesystem *get_devfs(void)
{
   return devfs;
}

/*
 * Registers the driver described by 'info'.
 * Returns driver's major number.
 */
int register_driver(driver_info *info)
{
   /* Be sure there's always enough space. */
   VERIFY(drivers_count < ARRAY_SIZE(drivers) - 1);

   drivers[drivers_count] = info;
   return drivers_count++;
}

typedef struct {

   list_node list;

   u32 dev_major;
   u32 dev_minor;
   const char *name;
   file_ops fops;
   devfs_entry_type type;

} devfs_file;

typedef struct {

   /* Yes, sub-directories are NOT supported at the moment */
   list_node files_list;

} devfs_directory;

typedef struct {

   devfs_directory root_dir;

   kmutex ex_mutex; // big exclusive whole-filesystem lock
                    // TODO: use a rw-lock when available in the kernel

   datetime_t wrt_time;

} devfs_data;

int create_dev_file(const char *filename, int major, int minor)
{
   ASSERT(devfs != NULL);

   if (major < 0 || major >= (int)drivers_count)
      return -EINVAL;

   filesystem *fs = devfs;
   driver_info *dinfo = drivers[major];

   devfs_data *d = fs->device_data;
   devfs_file *f = kmalloc(sizeof(devfs_file));

   if (!f)
      return -ENOMEM;

   list_node_init(&f->list);
   f->name = filename;
   f->dev_major = major;
   f->dev_minor = minor;

   int res = dinfo->create_dev_file(minor, &f->fops, &f->type);

   if (res < 0) {
      kfree2(f, sizeof(devfs_file));
      return res;
   }

   list_add_tail(&d->root_dir.files_list, &f->list);
   return 0;
}

ssize_t devfs_dir_read(fs_handle h, char *buf, size_t len)
{
   return -EINVAL;
}

ssize_t devfs_dir_write(fs_handle h, char *buf, size_t len)
{
   return -EINVAL;
}

off_t devfs_dir_seek(fs_handle h, off_t offset, int whence)
{
   return -EINVAL;
}

int devfs_dir_ioctl(fs_handle h, uptr request, void *arg)
{
   return -EINVAL;
}

int devfs_dir_stat64(fs_handle h, struct stat64 *statbuf)
{
   devfs_file_handle *dh = h;
   devfs_data *devfs_data = dh->fs->device_data;

   if (!h)
      return -ENOENT;

   bzero(statbuf, sizeof(struct stat64));
   statbuf->st_dev = dh->fs->device_id;
   statbuf->st_ino = 0;
   statbuf->st_mode = 0555 | S_IFDIR;
   statbuf->st_nlink = 1;
   statbuf->st_uid = 0; /* root */
   statbuf->st_gid = 0; /* root */
   statbuf->st_rdev = 0; /* device ID if a special file: in this case, NO. */
   statbuf->st_size = 0;
   statbuf->st_blksize = 4096;
   statbuf->st_blocks = statbuf->st_size / 512;

   statbuf->st_ctim.tv_sec = datetime_to_timestamp(devfs_data->wrt_time);
   statbuf->st_mtim.tv_sec = datetime_to_timestamp(devfs_data->wrt_time);
   statbuf->st_atim = statbuf->st_mtim;

   return 0;
}

int devfs_char_dev_stat64(fs_handle h, struct stat64 *statbuf)
{
   devfs_file_handle *dh = h;
   devfs_file *df = dh->devfs_file_ptr;
   devfs_data *devfs_data = dh->fs->device_data;

   bzero(statbuf, sizeof(struct stat64));

   statbuf->st_dev = dh->fs->device_id;
   statbuf->st_ino = 0;
   statbuf->st_mode = 0666;
   statbuf->st_nlink = 1;
   statbuf->st_uid = 0; /* root */
   statbuf->st_gid = 0; /* root */
   statbuf->st_rdev = df->dev_major << 8 | df->dev_minor;
   statbuf->st_size = 0;
   statbuf->st_blksize = 4096;
   statbuf->st_blocks = 0;

   if (dh->type == DEVFS_CHAR_DEVICE)
      statbuf->st_mode |= S_IFCHR;

   statbuf->st_ctim.tv_sec = datetime_to_timestamp(devfs_data->wrt_time);
   statbuf->st_mtim.tv_sec = datetime_to_timestamp(devfs_data->wrt_time);
   statbuf->st_atim = statbuf->st_mtim;

   return 0;
}

static int devfs_open_root_dir(filesystem *fs, fs_handle *out)
{
   devfs_file_handle *h;
   h = kzmalloc(sizeof(devfs_file_handle));

   if (!h)
      return -ENOMEM;

   h->type = DEVFS_DIRECTORY;
   h->fs = fs;
   h->fops = (file_ops) {
      .read = devfs_dir_read,
      .write = devfs_dir_write,
      .seek = devfs_dir_seek,
      .ioctl =  devfs_dir_ioctl,
      .stat = devfs_dir_stat64,
      .exlock = NULL,
      .exunlock = NULL,
      .shlock = NULL,
      .shunlock = NULL
   };

   *out = h;
   return 0;
}

static int devfs_open_file(filesystem *fs, devfs_file *pos, fs_handle *out)
{
   devfs_file_handle *h;
   h = kzmalloc(sizeof(devfs_file_handle));

   if (!h)
      return -ENOMEM;

   h->read_buf = kzmalloc(DEVFS_READ_BS);

   if (!h->read_buf) {
      kfree2(h, sizeof(devfs_file_handle));
      return -ENOMEM;
   }

   h->type = pos->type;
   h->devfs_file_ptr = pos;
   h->fs = fs;
   h->fops = pos->fops;

   if (!h->fops.stat)
      h->fops.stat = devfs_char_dev_stat64;

   *out = h;
   return 0;
}

static int devfs_open(filesystem *fs, const char *path, fs_handle *out)
{
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

   devfs_data *d = fs->device_data;
   devfs_file *pos, *temp;

   /*
    * Linearly iterate our linked list: we do not expect any time soon devfs
    * to contain more than a few files.
    */

   list_for_each(pos, temp, &d->root_dir.files_list, list) {
      if (!strcmp(pos->name, path)) {
         return devfs_open_file(fs, pos, out);
      }
   }

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
   kmutex_lock(&d->ex_mutex);
}

static void devfs_exclusive_unlock(filesystem *fs)
{
   devfs_data *d = fs->device_data;
   kmutex_unlock(&d->ex_mutex);
}

static void devfs_shared_lock(filesystem *fs)
{
   devfs_exclusive_lock(fs);
}

static void devfs_shared_unlock(filesystem *fs)
{
   devfs_exclusive_unlock(fs);
}

static int
devfs_getdents64(fs_handle h, struct linux_dirent64 *dirp, u32 buf_size)
{
   devfs_file_handle *dh = h;
   devfs_data *d = dh->fs->device_data;
   u32 offset = 0, curr_index = 0;
   struct linux_dirent64 ent;
   devfs_file *pos, *temp;

   if (dh->type != DEVFS_DIRECTORY)
      return -ENOTDIR;

   list_for_each(pos, temp, &d->root_dir.files_list, list) {

      if (curr_index < dh->read_pos) {
         curr_index++;
         continue;
      }

      const char *const file_name = pos->name;
      const u32 fl = strlen(file_name);
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
         return offset;
      }

      ent.d_ino = 0;
      ent.d_off = offset + entry_size;
      ent.d_reclen = entry_size;
      ent.d_type = DT_UNKNOWN;

      if (dh->type == DEVFS_CHAR_DEVICE)
         ent.d_type = DT_CHR;

      struct linux_dirent64 *user_ent = (void *)((char *)dirp + offset);

      if (copy_to_user(user_ent, &ent, sizeof(ent)) < 0)
         return -EFAULT;

      if (copy_to_user(user_ent->d_name, file_name, fl + 1) < 0)
         return -EFAULT;

      offset = ent.d_off;
      curr_index++;
      dh->read_pos++;
   }

   return offset;
}

filesystem *create_devfs(void)
{
   /* Disallow multiple instances of devfs */
   ASSERT(devfs == NULL);

   filesystem *fs = kzmalloc(sizeof(filesystem));

   if (!fs)
      return NULL;

   devfs_data *d = kzmalloc(sizeof(devfs_data));

   if (!d) {
      kfree2(fs, sizeof(filesystem));
      return NULL;
   }

   list_node_init(&d->root_dir.files_list);
   kmutex_init(&d->ex_mutex, KMUTEX_FL_RECURSIVE);

   read_system_clock_datetime(&d->wrt_time);
   fs->fs_type_name = "devfs";
   fs->flags = VFS_FS_RW;
   fs->device_id = vfs_get_new_device_id();
   fs->device_data = d;
   fs->open = devfs_open;
   fs->close = devfs_close;
   fs->dup = devfs_dup;
   fs->getdents64 = devfs_getdents64;

   fs->fs_exlock = devfs_exclusive_lock;
   fs->fs_exunlock = devfs_exclusive_unlock;
   fs->fs_shlock = devfs_shared_lock;
   fs->fs_shunlock = devfs_shared_unlock;

   return fs;
}

void create_and_register_devfs(void)
{
   devfs = create_devfs();

   if (!devfs)
      panic("Unable to create devfs");

   int rc = mountpoint_add(devfs, "/dev/");

   if (rc != 0)
      panic("mountpoint_add() failed with error: %d", rc);
}
