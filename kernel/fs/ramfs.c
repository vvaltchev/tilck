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
#include <tilck/kernel/bintree.h>
#include <tilck/kernel/paging.h>

#include <dirent.h> // system header

struct ramfs_inode;
typedef struct ramfs_inode ramfs_inode;

enum ramfs_entry {
   RAMFS_FILE,
   RAMFS_DIRECTORY,
   RAMFS_SYMLINK,
};

typedef struct {

   bintree_node node;
   uptr offset;                  /* MUST BE divisible by PAGE_SIZE */
   void *vaddr;

} ramfs_block;

typedef struct {

   list_node node;
   struct ramfs_inode *inode;
   char name[256 - sizeof(list_node) - sizeof(void *)];

} ramfs_entry;

struct ramfs_inode {

   int inode;
   int ref_count;
   enum ramfs_entry type;
   mode_t mode;                        /* permissions + special flags */

   union {
      ramfs_block *blocks_tree_root;   /* valid when type == RAMFS_FILE */
      list entries_list;               /* valid when type == RAMFS_DIRECTORY */
      const char *path;                /* valid when type == RAMFS_SYMLINK */
   };

   datetime_t ctime;
   datetime_t wtime;
};

typedef struct {

   /* fs_handle_base */
   FS_HANDLE_BASE_FIELDS

   /* ramfs-specific fields */
   ramfs_inode *inode;

   u32 read_pos;
   u32 write_pos;

} ramfs_handle;

typedef struct {

   rwlock_wp rwlock;

   int next_inode_num;
   ramfs_inode *root;

} ramfs_data;

static int
ramfs_dir_add_entry(ramfs_inode *idir, const char *iname, ramfs_inode *ie)
{
   ramfs_entry *e;
   const size_t enl = strlen(iname) + 1;
   ASSERT(idir->type == RAMFS_DIRECTORY);

   if (enl > sizeof(e->name))
      return -ENAMETOOLONG;

   if (!(e = kmalloc(sizeof(ramfs_entry))))
      return -ENOMEM;

   list_node_init(&e->node);
   e->inode = ie;
   memcpy(e->name, iname, enl);

   list_add_tail(&idir->entries_list, &e->node);
   ie->ref_count++;
   return 0;
}

static void
ramfs_dir_remove_entry(ramfs_inode *idir, ramfs_entry *e)
{
   ramfs_inode *i = e->inode;
   ASSERT(idir->type == RAMFS_DIRECTORY);

   list_remove(&e->node);
   i->ref_count--;
   kfree2(e, sizeof(ramfs_entry));
}

static ramfs_inode *
ramfs_create_inode_dir(ramfs_data *d, mode_t mode, ramfs_inode *parent)
{
   ramfs_inode *i = kzmalloc(sizeof(ramfs_inode));

   if (!i)
      return NULL;

   i->inode = d->next_inode_num++;
   i->type = RAMFS_DIRECTORY;
   i->mode = mode | S_IFDIR;

   list_init(&i->entries_list);

   if (ramfs_dir_add_entry(i, ".", i) < 0) {
      kfree2(i, sizeof(ramfs_inode));
      return NULL;
   }

   if (!parent)
      parent = i;

   if (ramfs_dir_add_entry(i, "..", parent) < 0) {

      ramfs_entry *e = list_first_obj(&i->entries_list, ramfs_entry, node);
      ramfs_dir_remove_entry(i, e);

      kfree2(i, sizeof(ramfs_inode));
      return NULL;
   }

   read_system_clock_datetime(&i->ctime);
   i->wtime = i->ctime;
   return i;
}

static int ramfs_destroy_inode(ramfs_data *d, ramfs_inode *i)
{
   if (i->type == RAMFS_DIRECTORY) {

      if (!list_is_empty(&i->entries_list))
         return -ENOTEMPTY;
   }

   kfree2(i, sizeof(ramfs_inode));
   return 0;
}

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

ssize_t ramfs_dir_read(fs_handle h, char *buf, size_t len)
{
   return -EINVAL;
}

ssize_t ramfs_dir_write(fs_handle h, char *buf, size_t len)
{
   return -EINVAL;
}

off_t ramfs_dir_seek(fs_handle h, off_t offset, int whence)
{
   return -EINVAL;
}

int ramfs_dir_ioctl(fs_handle h, uptr request, void *arg)
{
   return -EINVAL;
}

int ramfs_stat64(fs_handle h, struct stat64 *statbuf)
{
   if (!h)
      return -ENOENT;

   ramfs_handle *rh = h;
   ramfs_inode *inode = rh->inode;

   bzero(statbuf, sizeof(struct stat64));

   statbuf->st_dev = rh->fs->device_id;
   statbuf->st_ino = (typeof(statbuf->st_ino)) inode->inode;
   statbuf->st_mode = inode->mode;
   statbuf->st_nlink = (typeof(statbuf->st_nlink)) inode->ref_count;
   statbuf->st_uid = 0;  /* root */
   statbuf->st_gid = 0;  /* root */
   statbuf->st_rdev = 0; /* device ID if a special file: therefore, NO. */
   statbuf->st_size = 0;
   statbuf->st_blksize = PAGE_SIZE;
   statbuf->st_blocks = statbuf->st_size / 512;

   statbuf->st_ctim.tv_sec = datetime_to_timestamp(inode->ctime);
   statbuf->st_mtim.tv_sec = datetime_to_timestamp(inode->wtime);
   statbuf->st_atim = statbuf->st_mtim;

   return 0;
}

static int ramfs_open_dir(filesystem *fs, ramfs_inode *inode, fs_handle *out)
{
   ramfs_handle *h;

   if (!(h = kzmalloc(sizeof(ramfs_handle))))
      return -ENOMEM;

   h->inode = inode;
   h->fs = fs;
   h->fops = (file_ops) {
      .read = ramfs_dir_read,
      .write = ramfs_dir_write,
      .seek = ramfs_dir_seek,
      .ioctl = ramfs_dir_ioctl,
      .stat = ramfs_stat64,
      .exlock = NULL,
      .exunlock = NULL,
      .shlock = NULL,
      .shunlock = NULL,
   };

   *out = h;
   return 0;
}

static int ramfs_open_file(filesystem *fs, ramfs_inode *inode, fs_handle *out)
{
   ramfs_handle *h;

   if (!(h = kzmalloc(sizeof(ramfs_handle))))
      return -ENOMEM;

   h->inode = inode;
   h->fs = fs;
   h->fops = (file_ops) {
      .read = NULL,
      .write = NULL,
      .seek = NULL,
      .ioctl = NULL,
      .stat = ramfs_stat64,
      .exlock = NULL,
      .exunlock = NULL,
      .shlock = NULL,
      .shunlock = NULL,
   };

   *out = h;
   return 0;
}

static int
ramfs_open(filesystem *fs, const char *path, fs_handle *out, int fl, mode_t mod)
{
   ramfs_entry *pos;
   ramfs_data *d = fs->device_data;

   /*
    * Path is expected to be striped from the mountpoint prefix, but the '/'
    * is kept. In other words, /dev/tty is /tty here.
    */

   ASSERT(*path == '/');
   path++;

   if (!*path)
      return ramfs_open_dir(fs, d->root, out);

   // TEMP CODE: support only a single flat directory

   list_for_each_ro(pos, &d->root->entries_list, node) {
      if (!strcmp(pos->name, path)) {
         return ramfs_open_file(fs, pos->inode, out);
      }
   }

   return -ENOENT;
}

static void ramfs_close(fs_handle h)
{
   ramfs_handle *rh = h;
   kfree2(rh, sizeof(ramfs_handle));
}

static int
ramfs_getdents64(fs_handle h, struct linux_dirent64 *dirp, u32 buf_size)
{
   ramfs_handle *rh = h;
   u32 offset = 0, curr_index = 0;
   ramfs_inode *inode = rh->inode;
   struct linux_dirent64 ent;
   ramfs_entry *pos;

   if (inode->type != RAMFS_DIRECTORY)
      return -ENOTDIR;

   list_for_each_ro(pos, &inode->entries_list, node) {

      if (curr_index < rh->read_pos) {
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

      ent.d_ino = (typeof(ent.d_ino)) pos->inode->inode;
      ent.d_off = (s64)(offset + entry_size);
      ent.d_reclen = (u16)entry_size;
      ent.d_type = DT_UNKNOWN;

      struct linux_dirent64 *user_ent = (void *)((char *)dirp + offset);

      if (copy_to_user(user_ent, &ent, sizeof(ent)) < 0)
         return -EFAULT;

      if (copy_to_user(user_ent->d_name, file_name, fl + 1) < 0)
         return -EFAULT;

      offset = (u32) ent.d_off; /* s64 to u32 precision drop */
      curr_index++;
      rh->read_pos++;
   }

   return (int)offset;
}

void ramfs_destroy(filesystem *fs)
{
   ramfs_data *d = fs->device_data;

   if (d) {

      if (d->root) {
         ramfs_destroy_inode(d, d->root);
      }

      rwlock_wp_destroy(&d->rwlock);
      kfree2(d, sizeof(ramfs_data));
   }

   kfree2(fs, sizeof(filesystem));
}

filesystem *ramfs_create(void)
{
   filesystem *fs;
   ramfs_data *d;

   if (!(fs = kzmalloc(sizeof(filesystem))))
      return NULL;

   if (!(d = kzmalloc(sizeof(ramfs_data)))) {
      ramfs_destroy(fs);
      return NULL;
   }

   fs->device_data = d;
   rwlock_wp_init(&d->rwlock);
   d->next_inode_num = 1;
   d->root = ramfs_create_inode_dir(d, 0777, NULL);

   if (!d->root) {
      ramfs_destroy(fs);
      return NULL;
   }

   d->root->ref_count++;
   fs->fs_type_name = "ramfs";
   fs->device_id = vfs_get_new_device_id();
   fs->flags = VFS_FS_RW;

   fs->open = ramfs_open;
   fs->close = ramfs_close;
   fs->dup = NULL;
   fs->getdents64 = ramfs_getdents64;

   fs->fs_exlock = ramfs_exclusive_lock;
   fs->fs_exunlock = ramfs_exclusive_unlock;
   fs->fs_shlock = ramfs_shared_lock;
   fs->fs_shunlock = ramfs_shared_unlock;

   // //tmp
   // {
   //    ramfs_inode *i1 = ramfs_create_inode_dir(d, 0777, d->root);
   //    int rc = ramfs_dir_add_entry(d->root, "dir1", i1);
   //    VERIFY(rc == 0);
   // }
   // //end tmp
   return fs;
}

