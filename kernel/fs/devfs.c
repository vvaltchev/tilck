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

static struct fs *devfs;

/*
 * Registered drivers.
 */
static struct driver_info *drivers[32];
static u32 drivers_count;

struct fs *get_devfs(void)
{
   return devfs;
}

struct driver_info *get_driver_info(u16 major)
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
int register_driver(struct driver_info *info, int arg_major)
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

struct devfs_dir {

   /*
    * Yes, sub-directories are NOT supported by devfs. The whole struct fs is
    * just one flat directory.
    */
   enum vfs_entry_type type;
   struct list files_list;
   tilck_ino_t inode;
};

struct devfs_data {

   struct devfs_dir root_dir;
   struct rwlock_wp rwlock;
   time_t wrt_time;
   tilck_ino_t next_inode;
};

static inline tilck_ino_t devfs_get_next_inode(struct devfs_data *d)
{
   return d->next_inode++;
}

int create_dev_file(const char *filename, u16 major, u16 minor)
{
   ASSERT(devfs != NULL);

   struct fs *fs = devfs;
   struct driver_info *dinfo = get_driver_info(major);

   if (!dinfo)
      return -EINVAL;

   struct devfs_data *d = fs->device_data;
   struct devfs_file *f = kmalloc(sizeof(struct devfs_file));

   if (!f)
      return -ENOMEM;

   f->inode = devfs_get_next_inode(d);
   list_node_init(&f->dir_node);
   f->name = filename;
   f->dev_major = major;
   f->dev_minor = minor;

   int res = dinfo->create_dev_file(minor, &f->fops, &f->type);

   if (res < 0) {
      kfree2(f, sizeof(struct devfs_file));
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

static offt devfs_dir_seek(fs_handle h, offt target_off, int whence)
{
   struct devfs_handle *dh = h;
   struct devfs_data *d = dh->fs->device_data;
   struct devfs_file *pos;
   offt off = 0;

   if (target_off < 0 || whence != SEEK_SET)
      return -EINVAL;

   list_for_each_ro(pos, &d->root_dir.files_list, dir_node) {

      if (off == target_off)
         break;

      off++;
   }

   if (off == target_off) {
      dh->dpos = pos;
      dh->pos = off;
      return dh->pos;
   }

   return -EINVAL;
}

static int devfs_dir_ioctl(fs_handle h, uptr request, void *arg)
{
   return -EINVAL;
}

int devfs_stat(struct fs *fs, vfs_inode_ptr_t i, struct stat64 *statbuf)
{
   struct devfs_file *df = i;
   struct devfs_data *ddata = fs->device_data;

   bzero(statbuf, sizeof(struct stat64));

   statbuf->st_dev = fs->device_id;

   switch (df->type) {

      case VFS_DIR:
         ASSERT(i == &ddata->root_dir);
         statbuf->st_mode = 0555 | S_IFDIR;
         statbuf->st_ino = ddata->root_dir.inode;
         break;

      case VFS_CHAR_DEV:
         statbuf->st_mode = 0666 | S_IFCHR;
         statbuf->st_ino = df->inode;
         break;

      default:
         panic("[devfs] Invalid type: %d", df->type);
   }

   statbuf->st_nlink = 1;
   statbuf->st_uid = 0; /* root */
   statbuf->st_gid = 0; /* root */

   if (df->type == VFS_CHAR_DEV)
      statbuf->st_rdev = (dev_t)(df->dev_major << 8 | df->dev_minor);

   statbuf->st_size = 0;
   statbuf->st_blksize = PAGE_SIZE;
   statbuf->st_blocks = 0;

   statbuf->st_ctim.tv_sec = ddata->wrt_time;
   statbuf->st_mtim = statbuf->st_ctim;
   statbuf->st_atim = statbuf->st_mtim;

   return 0;
}

static const struct file_ops static_ops_devfs =
{
   .read = devfs_dir_read,
   .write = devfs_dir_write,
   .seek = devfs_dir_seek,
   .ioctl = devfs_dir_ioctl,
   .mmap = NULL,
   .munmap = NULL,
};

static int devfs_open_root_dir(struct fs *fs, fs_handle *out)
{
   struct devfs_handle *h;

   if (!(h = kzmalloc(sizeof(struct devfs_handle))))
      return -ENOMEM;

   h->type = VFS_DIR;
   h->fs = fs;
   h->fops = &static_ops_devfs;

   *out = h;
   return 0;
}

static int
devfs_open_file(struct fs *fs, struct devfs_file *pos, fs_handle *out)
{
   struct devfs_handle *h;

   if (!(h = kzmalloc(sizeof(struct devfs_handle))))
      return -ENOMEM;

   if (!(h->read_buf = kzmalloc(DEVFS_READ_BS))) {
      kfree2(h, sizeof(struct devfs_handle));
      return -ENOMEM;
   }

   h->type  = pos->type;
   h->file  = pos;
   h->fs    = fs;
   h->fops  = pos->fops;

   *out = h;
   return 0;
}

CREATE_FS_PATH_STRUCT(devfs_path, struct devfs_file *, struct devfs_file *);

static int
devfs_open(struct vfs_path *p, fs_handle *out, int fl, mode_t mod)
{
   struct devfs_path *dp = (struct devfs_path *) &p->fs_path;

   if (dp->inode) {

      if (dp->type == VFS_DIR)
         return devfs_open_root_dir(p->fs, out);

      if ((fl & O_CREAT) && (fl & O_EXCL))
         return -EEXIST;

      return devfs_open_file(p->fs, dp->inode, out);
   }

   if (fl & O_CREAT)
      return -EROFS;

   return -ENOENT;
}

static void devfs_close(fs_handle h)
{
   struct devfs_handle *devh = h;
   kfree2(devh->read_buf, DEVFS_READ_BS);
   kfree2(devh->write_buf, DEVFS_WRITE_BS);
   kfree2(devh, sizeof(struct devfs_handle));
}

static int devfs_dup(fs_handle fsh, fs_handle *dup_h)
{
   struct devfs_handle *h = fsh;
   struct devfs_handle *h2;
   h2 = kzmalloc(sizeof(struct devfs_handle));

   if (!h2)
      return -ENOMEM;

   memcpy(h2, h, sizeof(struct devfs_handle));

   if (h->read_buf) {

      h2->read_buf = kmalloc(DEVFS_READ_BS);

      if (!h2->read_buf) {
         kfree2(h2, sizeof(struct devfs_handle));
         return -ENOMEM;
      }

      memcpy(h2->read_buf, h->read_buf, DEVFS_READ_BS);
   }

   if (h->write_buf) {

      h2->write_buf = kmalloc(DEVFS_WRITE_BS);

      if (!h2->write_buf) {
         kfree2(h->read_buf, DEVFS_READ_BS);
         kfree2(h2, sizeof(struct devfs_handle));
         return -ENOMEM;
      }
   }

   *dup_h = h2;
   return 0;
}

static void devfs_exclusive_lock(struct fs *fs)
{
   struct devfs_data *d = fs->device_data;
   rwlock_wp_exlock(&d->rwlock);
}

static void devfs_exclusive_unlock(struct fs *fs)
{
   struct devfs_data *d = fs->device_data;
   rwlock_wp_exunlock(&d->rwlock);
}

static void devfs_shared_lock(struct fs *fs)
{
   struct devfs_data *d = fs->device_data;
   rwlock_wp_shlock(&d->rwlock);
}

static void devfs_shared_unlock(struct fs *fs)
{
   struct devfs_data *d = fs->device_data;
   rwlock_wp_shunlock(&d->rwlock);
}

static int devfs_getdents(fs_handle h, get_dents_func_cb vfs_cb, void *arg)
{
   struct devfs_handle *dh = h;
   struct devfs_data *d = dh->fs->device_data;
   int rc = 0;

   if (dh->type != VFS_DIR)
      return -ENOTDIR;

   if (!dh->dpos) {
      dh->dpos = list_first_obj(
         &d->root_dir.files_list, struct devfs_file, dir_node
      );
   }

   list_for_each_ro_kp(dh->dpos, &d->root_dir.files_list, dir_node) {

      struct vfs_dent64 dent = {
         .ino  = dh->dpos->inode,
         .type = dh->dpos->type,
         .name_len = (u8) strlen(dh->dpos->name) + 1,
         .name = dh->dpos->name,
      };

      if ((rc = vfs_cb(&dent, arg)))
         break;
   }

   return rc;
}

static void
devfs_get_entry(struct fs *fs,
                void *dir_inode,
                const char *name,
                ssize_t nl,
                struct fs_path *fs_path)
{
   struct devfs_data *d = fs->device_data;
   struct devfs_dir *dir;
   struct devfs_file *pos;

   if ((!dir_inode && !name) || is_dot_or_dotdot(name, (int)nl)) {

      *fs_path = (struct fs_path) {
         .inode      = &d->root_dir,
         .dir_inode  = &d->root_dir,
         .dir_entry  = NULL,
         .type       = VFS_DIR,
      };

      return;
   }

   dir = dir_inode;
   bzero(fs_path, sizeof(*fs_path));

   list_for_each_ro(pos, &dir->files_list, dir_node) {
      if (!strncmp(pos->name, name, (size_t)nl))
         if (!pos->name[nl])
            break;
   }

   if (&pos->dir_node != (struct list_node *) &dir->files_list) {
      *fs_path = (struct fs_path) {
         .inode         = pos,
         .dir_inode     = dir,
         .dir_entry     = pos,
         .type          = VFS_FILE,
      };
   }
}

static vfs_inode_ptr_t devfs_get_inode(fs_handle h)
{
   return ((struct devfs_handle *)h)->file;
}

static int devfs_retain_inode(struct fs *fs, vfs_inode_ptr_t inode)
{
   /* devfs does not support removal of inodes after boot */
   return 1;
}

static int devfs_release_inode(struct fs *fs, vfs_inode_ptr_t inode)
{
   /* devfs does not support removal of inodes after boot */
   return 1;
}

static const struct fs_ops static_fsops_devfs =
{
   .get_inode = devfs_get_inode,
   .open = devfs_open,
   .close = devfs_close,
   .dup = devfs_dup,
   .getdents = devfs_getdents,
   .unlink = NULL,
   .mkdir = NULL,
   .rmdir = NULL,
   .truncate = NULL,
   .stat = devfs_stat,
   .chmod = NULL,
   .get_entry = devfs_get_entry,
   .rename = NULL,
   .link = NULL,
   .retain_inode = devfs_retain_inode,
   .release_inode = devfs_release_inode,

   .fs_exlock = devfs_exclusive_lock,
   .fs_exunlock = devfs_exclusive_unlock,
   .fs_shlock = devfs_shared_lock,
   .fs_shunlock = devfs_shared_unlock,
};

struct fs *create_devfs(void)
{
   struct fs *fs;
   struct devfs_data *d;

   /* Disallow multiple instances of devfs */
   ASSERT(devfs == NULL);

   if (!(fs = kzmalloc(sizeof(struct fs))))
      return NULL;

   if (!(d = kzmalloc(sizeof(struct devfs_data)))) {
      kfree2(fs, sizeof(struct fs));
      return NULL;
   }

   d->next_inode = 1;
   d->root_dir.type = VFS_DIR;
   d->root_dir.inode = devfs_get_next_inode(d);
   list_init(&d->root_dir.files_list);
   rwlock_wp_init(&d->rwlock, false);
   d->wrt_time = read_system_clock_timestamp();

   fs->fs_type_name = "devfs";
   fs->device_id = vfs_get_new_device_id();
   fs->flags = VFS_FS_RW;
   fs->device_data = d;
   fs->fsops = &static_fsops_devfs;

   return fs;
}

void init_devfs(void)
{
   int rc;
   devfs = create_devfs();

   if (!devfs)
      panic("Unable to create devfs");

   if ((rc = mp_add(devfs, "/dev/")))
      panic("mp_add() failed with error: %d", rc);
}
