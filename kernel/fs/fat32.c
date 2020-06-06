/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/fs/fat32.h>
#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/datetime.h>
#include <tilck/kernel/user.h>

#include <dirent.h> // system header

int fat_mmap(struct user_mapping *um, bool register_only);
int fat_munmap(fs_handle h, void *vaddrp, size_t len);
int fat_ramdisk_prepare_for_mmap(struct fat_fs_device_data *d, size_t rd_size);

/*
 * Special fat_walk() wrapper handling the special case where `e` is NOT a dir
 * entry but a pointer to the entries in the root directory.
 */

static inline int
fat_fs_walk_generic(struct fat_fs_device_data *d,
                    struct fat_walk_static_params *static_walk_params,
                    struct fat_entry *e)
{
   return fat_walk(static_walk_params,
                   e == d->root_dir_entries
                     ? d->root_cluster
                     : fat_get_first_cluster(e));
}

STATIC void
fat_close(fs_handle handle)
{
   struct fatfs_handle *h = (struct fatfs_handle *)handle;
   kfree2(h, sizeof(struct fatfs_handle));
}

STATIC ssize_t
fat_read(fs_handle handle, char *buf, size_t bufsize)
{
   struct fatfs_handle *h = (struct fatfs_handle *) handle;
   struct fat_fs_device_data *d = h->fs->device_data;
   offt fsize = (offt)h->e->DIR_FileSize;
   offt written_to_buf = 0;

   if (h->pos >= fsize) {

      /*
       * The cursor is at the end or past the end: nothing to read.
       */

      return 0;
   }

   do {

      char *data = fat_get_pointer_to_cluster_data(d->hdr, h->curr_cluster);

      const offt file_rem       = fsize - h->pos;
      const offt buf_rem        = (offt)bufsize - written_to_buf;
      const offt cluster_off    = h->pos % (offt)d->cluster_size;
      const offt cluster_rem    = (offt)d->cluster_size - cluster_off;
      const offt to_read        = MIN3(cluster_rem, buf_rem, file_rem);

      ASSERT(to_read >= 0);

      memcpy(buf + written_to_buf, data + cluster_off, (size_t)to_read);
      written_to_buf += to_read;
      h->pos += to_read;

      if (to_read < cluster_rem) {

         /*
          * We read less than cluster_rem because the buf was not big enough
          * or because the file was not big enough. In either case, we cannot
          * continue.
          */
         break;
      }

      // find the next cluster
      u32 fatval = fat_read_fat_entry(d->hdr, d->type, 0, h->curr_cluster);

      if (fat_is_end_of_clusterchain(d->type, fatval)) {
         ASSERT(h->pos == fsize);
         break;
      }

      // we do not expect BAD CLUSTERS
      ASSERT(!fat_is_bad_cluster(d->type, fatval));

      h->curr_cluster = fatval; // go reading the new cluster in the chain.

   } while (true);

   return (ssize_t)written_to_buf;
}


STATIC int
fat_rewind(fs_handle handle)
{
   struct fatfs_handle *h = (struct fatfs_handle *) handle;
   h->pos = 0;
   h->curr_cluster = fat_get_first_cluster(h->e);
   return 0;
}

STATIC offt
fat_seek_forward(fs_handle handle, offt dist)
{
   struct fatfs_handle *h = (struct fatfs_handle *) handle;
   struct fat_fs_device_data *d = h->fs->device_data;
   offt fsize = (offt)h->e->DIR_FileSize;
   offt moved_distance = 0;

   if (dist == 0)
      return h->pos;

   if (h->pos + dist > fsize) {
      /* Allow, like Linux does, to seek past the end of a file. */
      h->pos += dist;
      h->curr_cluster = (u32) -1; /* invalid cluster */
      return (offt) h->pos;
   }

   do {

      const offt file_rem       = fsize - h->pos;
      const offt dist_rem       = dist - moved_distance;
      const offt cluster_off    = h->pos % (offt)d->cluster_size;
      const offt cluster_rem    = (offt)d->cluster_size - cluster_off;
      const offt to_move        = MIN3(cluster_rem, dist_rem, file_rem);

      ASSERT(to_move >= 0);

      moved_distance += to_move;
      h->pos += to_move;

      if (to_move < cluster_rem)
         break;

      // find the next cluster
      u32 fatval = fat_read_fat_entry(d->hdr, d->type, 0, h->curr_cluster);

      if (fat_is_end_of_clusterchain(d->type, fatval)) {
         ASSERT(h->pos == fsize);
         break;
      }

      // we do not expect BAD CLUSTERS
      ASSERT(!fat_is_bad_cluster(d->type, fatval));

      h->curr_cluster = fatval; // go reading the new cluster in the chain.

   } while (true);

   return (offt)h->pos;
}

struct fat_count_dirents_ctx {
   offt count;
};

static int
fat_count_dirents_cb(struct fat_hdr *hdr,
                     enum fat_type ft,
                     struct fat_entry *entry,
                     const char *long_name,
                     void *arg)
{
   struct fat_count_dirents_ctx *ctx = arg;
   ctx->count++;
   return 0;
}

/*
 * Count the number of entries in a given FAT directory.
 * TODO: implement fat_count_dirents() in a more efficient way.
 */
STATIC offt fat_count_dirents(struct fat_fs_device_data *d, struct fat_entry *e)
{
   int rc;
   struct fat_count_dirents_ctx ctx = { .count = 0 };
   struct fat_walk_static_params walk_params = {
      .ctx = NULL,      /* no need for long name ctx */
      .h = d->hdr,
      .ft = d->type,
      .cb = &fat_count_dirents_cb,
      .arg = &ctx,
   };

   ASSERT(e->directory);
   rc = fat_fs_walk_generic(d, &walk_params, e);
   return rc ? rc : ctx.count;
}

static offt fat_seek_dir(struct fatfs_handle *fh, offt off)
{
   if (off < 0)
      return -EINVAL;

   if (off > fat_count_dirents(fh->fs->device_data, fh->e))
      return -EINVAL;

   fh->pos = off;
   return fh->pos;
}

STATIC offt
fat_seek(fs_handle handle, offt off, int whence)
{
   struct fatfs_handle *fh = handle;

   if (fh->e->directory) {

      if (whence != SEEK_SET)
         return -EINVAL;

      return fat_seek_dir(fh, off);
   }

   offt curr_pos = fh->pos;

   switch (whence) {

      case SEEK_SET:

         if (off < 0)
            return -EINVAL; /* invalid negative offset */

         fat_rewind(handle);
         break;

      case SEEK_END:

         if (off >= 0)
            break;

         struct fatfs_handle *h = (struct fatfs_handle *) handle;
         off = (offt) h->e->DIR_FileSize + off;

         if (off < 0)
            return -EINVAL;

         fat_rewind(handle);
         break;

      case SEEK_CUR:

         if (off < 0) {

            off = curr_pos + off;

            if (off < 0)
               return -EINVAL;

            fat_rewind(handle);
         }

         break;

      default:
         return -EINVAL;
   }

   return fat_seek_forward(handle, off);
}

struct datetime
fat_datetime_to_regular_datetime(u16 date, u16 time, u8 timetenth)
{
   struct datetime d;

   d.day = date & 0b11111;           // 5 bits: [0..4]
   d.month = (date >> 5) & 0b1111;   // 4 bits: [5..8]
   d.year = (date >> 9) & 0b1111111; // 7 bits: [9..15]
   d.year += 1980;

   d.sec = time & 0b11111;           // 5 bits: [0..4]
   d.min = (time >> 5) & 0b111111;   // 6 bits: [5..10]
   d.hour = (time >> 11) & 0b11111;  // 5 bits: [11..15]

   d.sec += timetenth / 10;
   return d;
}

static inline tilck_ino_t
fat_entry_to_inode(struct fat_hdr *hdr, struct fat_entry *e)
{
   return (tilck_ino_t)((long)e - (long)hdr);
}

STATIC int fat_stat(struct fs *fs, vfs_inode_ptr_t i, struct stat64 *statbuf)
{
   struct fat_entry *e = i;
   struct datetime crt_time, wrt_time;

   if (!e)
      return -ENOENT;

   bzero(statbuf, sizeof(struct stat64));

   statbuf->st_dev = fs->device_id;
   statbuf->st_ino = fat_entry_to_inode(fs->device_data, e);
   statbuf->st_mode = 0777;
   statbuf->st_nlink = 1;
   statbuf->st_uid = 0; /* root */
   statbuf->st_gid = 0; /* root */
   statbuf->st_rdev = 0; /* device ID, if a special file */
   statbuf->st_size = e->DIR_FileSize;
   statbuf->st_blksize = 4096;
   statbuf->st_blocks = statbuf->st_size / 512;

   if (e->directory || e->volume_id)
      statbuf->st_mode |= S_IFDIR;
   else
      statbuf->st_mode |= S_IFREG;

   crt_time =
      fat_datetime_to_regular_datetime(e->DIR_CrtDate,
                                       e->DIR_CrtTime,
                                       e->DIR_CrtTimeTenth);

   wrt_time =
      fat_datetime_to_regular_datetime(e->DIR_WrtDate,
                                       e->DIR_WrtTime,
                                       0 /* No WrtTimeTenth */);

   statbuf->st_ctim.tv_sec = (time_t)datetime_to_timestamp(crt_time);
   statbuf->st_mtim.tv_sec = (time_t)datetime_to_timestamp(wrt_time);
   statbuf->st_atim = statbuf->st_mtim;
   return 0;
}

struct fat_getdents_ctx {

   struct fatfs_handle *fh;
   get_dents_func_cb vfs_cb;
   void *vfs_ctx;
   int rc;
};

static int
fat_getdents_cb(struct fat_hdr *hdr,
                enum fat_type ft,
                struct fat_entry *entry,
                const char *long_name,
                void *arg)
{
   char short_name[16];
   const char *entname = long_name ? long_name : short_name;
   struct fat_getdents_ctx *ctx = arg;

   if (entname == short_name)
      fat_get_short_name(entry, short_name);

   struct vfs_dent64 dent = {
      .ino  = fat_entry_to_inode(hdr, entry),
      .type = entry->directory ? VFS_DIR : VFS_FILE,
      .name_len = (u8) strlen(entname) + 1,
      .name = entname,
   };

   return ctx->vfs_cb(&dent, ctx->vfs_ctx);
}

static int fat_getdents(fs_handle h, get_dents_func_cb cb, void *arg)
{
   struct fatfs_handle *fh = h;
   struct fat_fs_device_data *d = fh->fs->device_data;
   struct fat_getdents_ctx ctx;
   struct fat_walk_long_name_ctx walk_ctx;
   struct fat_walk_static_params walk_params;
   int rc;

   if (!fh->e->directory && !fh->e->volume_id)
      return -ENOTDIR;

   ctx = (struct fat_getdents_ctx) {
      .fh = fh,
      .vfs_cb = cb,
      .vfs_ctx = arg,
      .rc = 0,
   };

   walk_params = (struct fat_walk_static_params) {
      .ctx = &walk_ctx,
      .h = d->hdr,
      .ft = d->type,
      .cb = &fat_getdents_cb,
      .arg = &ctx,
   };

   rc = fat_fs_walk_generic(d, &walk_params, fh->e);
   return rc ? rc : ctx.rc;
}

STATIC void fat_exclusive_lock(struct fs *fs)
{
   if (!(fs->flags & VFS_FS_RW))
      return; /* read-only: no lock is needed */

   NOT_IMPLEMENTED();
}

STATIC void fat_exclusive_unlock(struct fs *fs)
{
   if (!(fs->flags & VFS_FS_RW))
      return; /* read-only: no lock is needed */

   NOT_IMPLEMENTED();
}

STATIC void fat_shared_lock(struct fs *fs)
{
   if (!(fs->flags & VFS_FS_RW))
      return; /* read-only: no lock is needed */

   NOT_IMPLEMENTED();
}

STATIC void fat_shared_unlock(struct fs *fs)
{
   if (!(fs->flags & VFS_FS_RW))
      return; /* read-only: no lock is needed */

   NOT_IMPLEMENTED();
}

STATIC ssize_t fat_write(fs_handle h, char *buf, size_t len)
{
   struct fs *fs = get_fs(h);

   if (!(fs->flags & VFS_FS_RW))
      return -EBADF; /* read-only file system: can't write */

   NOT_IMPLEMENTED();
}

STATIC int fat_ioctl(fs_handle h, ulong request, void *arg)
{
   return -EINVAL;
}

static const struct file_ops static_ops_fat =
{
   .read = fat_read,
   .seek = fat_seek,
   .write = fat_write,
   .ioctl = fat_ioctl,
   .mmap = fat_mmap,
   .munmap = fat_munmap,
};

STATIC int
fat_open(struct vfs_path *p, fs_handle *out, int fl, mode_t mode)
{
   struct fatfs_handle *h;
   struct fs *fs = p->fs;
   struct fat_fs_path *fp = (struct fat_fs_path *)&p->fs_path;
   struct fat_entry *e = fp->entry;
   struct fat_fs_device_data *d = fs->device_data;

   if (!e) {

      if (!(fs->flags & VFS_FS_RW))
         if (fl & O_CREAT)
            return -EROFS;

      return -ENOENT;
   }

   if ((fl & O_CREAT) && (fl & O_EXCL))
      return -EEXIST;

   if (!(fs->flags & VFS_FS_RW))
      if (fl & (O_WRONLY | O_RDWR))
         return -EROFS;

   if (!(h = kzmalloc(sizeof(struct fatfs_handle))))
      return -ENOMEM;

   vfs_init_fs_handle_base_fields((void *)h, fs, &static_ops_fat);
   h->e = e;
   h->pos = 0;
   h->curr_cluster = fat_get_first_cluster(e);

   if (d->mmap_support)
      h->spec_flags = VFS_SPFL_MMAP_SUPPORTED;

   *out = h;
   return 0;
}

STATIC int fat_dup(fs_handle h, fs_handle *dup_h)
{
   struct fatfs_handle *new_h = kmalloc(sizeof(struct fatfs_handle));

   if (!new_h)
      return -ENOMEM;

   memcpy(new_h, h, sizeof(struct fatfs_handle));
   *dup_h = new_h;
   return 0;
}

static inline void
fat_get_root_entry(struct fat_fs_device_data *d, struct fat_fs_path *fp)
{
   *fp = (struct fat_fs_path) {
      .entry            = d->root_dir_entries,
      .parent_entry     = d->root_dir_entries,
      .unused           = NULL,
      .type             = VFS_DIR,
   };
}

static void
fat_get_entry(struct fs *fs,
              void *dir_inode,
              const char *name,
              ssize_t name_len,
              struct fs_path *fs_path)
{
   struct fat_fs_device_data *d = fs->device_data;
   struct fat_fs_path *fp = (struct fat_fs_path *)fs_path;
   struct fat_walk_static_params walk_params;
   struct fat_entry *dir_entry;
   struct fat_search_ctx ctx;

   if (!dir_inode && !name)              // both dir_inode and name are NULL:
      return fat_get_root_entry(d, fp);  // getting a path to the root dir

   dir_entry = dir_inode ? dir_inode : d->root_dir_entries;

   if (UNLIKELY(dir_entry == d->root_dir_entries))
      if (is_dot_or_dotdot(name, (int)name_len))
         return fat_get_root_entry(d, fp);

   walk_params = (struct fat_walk_static_params) {
      .ctx = &ctx.walk_ctx,
      .h = d->hdr,
      .ft = d->type,
      .cb = &fat_search_entry_cb,
      .arg = &ctx,
   };

   fat_init_search_ctx(&ctx, name, true);
   fat_fs_walk_generic(d, &walk_params, dir_entry);

   struct fat_entry *res = !ctx.not_dir ? ctx.result : NULL;
   enum vfs_entry_type type = VFS_NONE;

   if (res) {

      const u32 clu = fat_get_first_cluster(res);
      type = res->directory ? VFS_DIR : VFS_FILE;

      if (type == VFS_DIR && (clu == 0 || clu == d->root_cluster)) {
         res = d->root_dir_entries;
         type = VFS_DIR;
      }
   }

   *fp = (struct fat_fs_path) {
      .entry         = res,
      .parent_entry  = dir_entry,
      .unused        = NULL,
      .type          = type,
   };
}

static vfs_inode_ptr_t fat_get_inode(fs_handle h)
{
   return ((struct fatfs_handle *)h)->e;
}

static int fat_retain_inode(struct fs *fs, vfs_inode_ptr_t inode)
{
   if (fs->flags & VFS_FS_RW)
      NOT_IMPLEMENTED();

   return 1;
}

static int fat_release_inode(struct fs *fs, vfs_inode_ptr_t inode)
{
   if (fs->flags & VFS_FS_RW)
      NOT_IMPLEMENTED();

   return 1;
}

static const struct fs_ops static_fsops_fat =
{
   .get_inode = fat_get_inode,
   .open = fat_open,
   .close = fat_close,
   .dup = fat_dup,
   .getdents = fat_getdents,
   .unlink = NULL,
   .mkdir = NULL,
   .rmdir = NULL,
   .truncate = NULL,
   .stat = fat_stat,
   .chmod = NULL,
   .get_entry = fat_get_entry,
   .rename = NULL,
   .link = NULL,
   .retain_inode = fat_retain_inode,
   .release_inode = fat_release_inode,

   .fs_exlock = fat_exclusive_lock,
   .fs_exunlock = fat_exclusive_unlock,
   .fs_shlock = fat_shared_lock,
   .fs_shunlock = fat_shared_unlock,
};

struct fs *fat_mount_ramdisk(void *vaddr, size_t rd_size, u32 flags)
{
   if (flags & VFS_FS_RW)
      panic("fat_mount_ramdisk: r/w mode is NOT currently supported");

   struct fat_fs_device_data *d = kzmalloc(sizeof(struct fat_fs_device_data));

   if (!d)
      return NULL;

   d->hdr = (struct fat_hdr *) vaddr;
   d->type = fat_get_type(d->hdr);
   d->cluster_size = d->hdr->BPB_SecPerClus * d->hdr->BPB_BytsPerSec;
   d->root_dir_entries = fat_get_rootdir(d->hdr, d->type, &d->root_cluster);

   struct fs *fs = kzmalloc(sizeof(struct fs));

   if (!fs) {
      kfree2(d, sizeof(struct fat_fs_device_data));
      return NULL;
   }

   fs->fs_type_name = "fat";
   fs->flags = flags;
   fs->device_id = vfs_get_new_device_id();
   fs->device_data = d;
   fs->fsops = &static_fsops_fat;
   fs->flags |= VFS_FS_RQ_DE_SKIP;

   if (!fat_ramdisk_prepare_for_mmap(d, rd_size))
      d->mmap_support = true;

   return fs;
}

void fat_umount_ramdisk(struct fs *fs)
{
   kfree2(fs->device_data, sizeof(struct fat_fs_device_data));
   kfree2(fs, sizeof(struct fs));
}
