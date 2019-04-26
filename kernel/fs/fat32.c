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

/*
 * Generic version of fat_get_first_cluster() that works also when `e` is the
 * root entry.
 */

static inline u32
fat_get_first_cluster_generic(fat_fs_device_data *d, fat_entry *e)
{
   return e == d->root_entry ? d->root_cluster : fat_get_first_cluster(e);
}

STATIC void
fat_close(fs_handle handle)
{
   fat_file_handle *h = (fat_file_handle *)handle;
   kfree2(h, sizeof(fat_file_handle));
}

STATIC ssize_t
fat_read(fs_handle handle, char *buf, size_t bufsize)
{
   fat_file_handle *h = (fat_file_handle *) handle;
   fat_fs_device_data *d = h->fs->device_data;
   off_t fsize = (off_t)h->e->DIR_FileSize;
   off_t written_to_buf = 0;

   if (h->pos >= fsize) {

      /*
       * The cursor is at the end or past the end: nothing to read.
       */

      return 0;
   }

   do {

      char *data = fat_get_pointer_to_cluster_data(d->hdr, h->curr_cluster);

      const off_t file_rem       = fsize - h->pos;
      const off_t buf_rem        = (off_t)bufsize - written_to_buf;
      const off_t cluster_off    = h->pos % (off_t)d->cluster_size;
      const off_t cluster_rem    = (off_t)d->cluster_size - cluster_off;
      const off_t to_read        = MIN3(cluster_rem, buf_rem, file_rem);

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
      u32 fatval = fat_read_fat_entry(d->hdr, d->type, h->curr_cluster, 0);

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
   fat_file_handle *h = (fat_file_handle *) handle;
   h->pos = 0;
   h->curr_cluster = fat_get_first_cluster(h->e);
   return 0;
}

STATIC off_t
fat_seek_forward(fs_handle handle, off_t dist)
{
   fat_file_handle *h = (fat_file_handle *) handle;
   fat_fs_device_data *d = h->fs->device_data;
   off_t fsize = (off_t)h->e->DIR_FileSize;
   off_t moved_distance = 0;

   if (dist == 0)
      return h->pos;

   if (h->pos + dist > fsize) {
      /* Allow, like Linux does, to seek past the end of a file. */
      h->pos += dist;
      h->curr_cluster = (u32) -1; /* invalid cluster */
      return (off_t) h->pos;
   }

   do {

      const off_t file_rem       = fsize - h->pos;
      const off_t dist_rem       = dist - moved_distance;
      const off_t cluster_off    = h->pos % (off_t)d->cluster_size;
      const off_t cluster_rem    = (off_t)d->cluster_size - cluster_off;
      const off_t to_move        = MIN3(cluster_rem, dist_rem, file_rem);

      ASSERT(to_move >= 0);

      moved_distance += to_move;
      h->pos += to_move;

      if (to_move < cluster_rem)
         break;

      // find the next cluster
      u32 fatval = fat_read_fat_entry(d->hdr, d->type, h->curr_cluster, 0);

      if (fat_is_end_of_clusterchain(d->type, fatval)) {
         ASSERT(h->pos == fsize);
         break;
      }

      // we do not expect BAD CLUSTERS
      ASSERT(!fat_is_bad_cluster(d->type, fatval));

      h->curr_cluster = fatval; // go reading the new cluster in the chain.

   } while (true);

   return (off_t)h->pos;
}

STATIC off_t
fat_seek(fs_handle handle, off_t off, int whence)
{
   off_t curr_pos = (off_t) ((fat_file_handle *)handle)->pos;

   switch (whence) {

   case SEEK_SET:

      if (off < 0)
         return -EINVAL; /* invalid negative offset */

      fat_rewind(handle);
      break;

   case SEEK_END:

      if (off >= 0)
         break;

      fat_file_handle *h = (fat_file_handle *) handle;
      off = (off_t) h->e->DIR_FileSize + off;

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

datetime_t
fat_datetime_to_regular_datetime(u16 date, u16 time, u8 timetenth)
{
   datetime_t d;

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

static inline ino_t fat_handle_to_inode(fat_file_handle *fh)
{
   fat_fs_device_data *d = fh->fs->device_data;
   return (ino_t)((sptr)fh->e - (sptr)d->hdr);
}

STATIC int fat_stat64(fs_handle h, struct stat64 *statbuf)
{
   fat_file_handle *fh = h;
   datetime_t crt_time, wrt_time;

   if (!h)
      return -ENOENT;

   bzero(statbuf, sizeof(struct stat64));

   statbuf->st_dev = fh->fs->device_id;
   statbuf->st_ino = fat_handle_to_inode(fh);
   statbuf->st_mode = 0555;
   statbuf->st_nlink = 1;
   statbuf->st_uid = 0; /* root */
   statbuf->st_gid = 0; /* root */
   statbuf->st_rdev = 0; /* device ID, if a special file */
   statbuf->st_size = fh->e->DIR_FileSize;
   statbuf->st_blksize = 4096;
   statbuf->st_blocks = statbuf->st_size / 512;

   if (fh->e->directory || fh->e->volume_id)
      statbuf->st_mode |= S_IFDIR;
   else
      statbuf->st_mode |= S_IFREG;

   crt_time =
      fat_datetime_to_regular_datetime(fh->e->DIR_CrtDate,
                                       fh->e->DIR_CrtTime,
                                       fh->e->DIR_CrtTimeTenth);

   wrt_time =
      fat_datetime_to_regular_datetime(fh->e->DIR_WrtDate,
                                       fh->e->DIR_WrtTime,
                                       0 /* No WrtTimeTenth */);

   statbuf->st_ctim.tv_sec = datetime_to_timestamp(crt_time);
   statbuf->st_mtim.tv_sec = datetime_to_timestamp(wrt_time);
   statbuf->st_atim = statbuf->st_mtim;
   return 0;
}

typedef struct {

   /* input variables */
   struct linux_dirent64 *dirp;
   u32 buf_size;
   fat_file_handle *fh;

   /* context variables */
   u32 offset;
   off_t curr_file_index;

   /* output variables */
   ssize_t rc;

} getdents64_walk_ctx;

STATIC int
fat_getdents64_cb(fat_header *hdr,
                  fat_type ft,
                  fat_entry *entry,
                  const char *long_name,
                  void *arg)
{
   char short_name[16];
   const char *file_name = long_name ? long_name : short_name;
   getdents64_walk_ctx *ctx = arg;
   struct linux_dirent64 ent;
   int rc;

   /* Reuse `pos` as file index in case a directory has been opened */
   if (ctx->curr_file_index < ctx->fh->pos) {
      ctx->curr_file_index++;
      return 0;
   }

   if (file_name == short_name)
      fat_get_short_name(entry, short_name);

   const u32 fl = (u32)strlen(file_name);
   const u32 entry_size = fl + 1 + sizeof(struct linux_dirent64);

   if (ctx->offset + entry_size > ctx->buf_size) {

      if (!ctx->offset) {

         /*
          * We haven't "returned" any entries yet and the buffer is too small
          * for our first entry.
          */

         ctx->rc = -EINVAL;
         return -1; /* stop the walk */
      }

      /* We "returned" at least one entry */
      return -1; /* stop the walk */
   }

   ent.d_ino = 0;
   ent.d_off = (s64)(ctx->offset + entry_size);
   ent.d_reclen = (u16) entry_size;
   ent.d_type = entry->directory ? DT_DIR : DT_REG;

   struct linux_dirent64 *user_ent = (void *)((char *)ctx->dirp + ctx->offset);
   rc = copy_to_user(user_ent, &ent, sizeof(ent));

   if (rc < 0) {
      ctx->rc = -EFAULT;
      return -1; /* stop the walk */
   }

   rc = copy_to_user(user_ent->d_name, file_name, fl + 1);

   if (rc < 0) {
      ctx->rc = -EFAULT;
      return -1; /* stop the walk */
   }

   ctx->offset = (u32) ent.d_off; /* s64 to u32 precision drop */
   ctx->curr_file_index++;
   ctx->fh->pos++;
   return 0;
}

STATIC int
fat_getdents64(fs_handle h, struct linux_dirent64 *dirp, u32 buf_size)
{
   fat_file_handle *fh = h;
   int rc;

   if (!fh->e->directory && !fh->e->volume_id)
      return -ENOTDIR;

   fat_fs_device_data *dd = fh->fs->device_data;
   fat_walk_dir_ctx walk_ctx = {0};

   getdents64_walk_ctx ctx = {
      .dirp = dirp,
      .buf_size = buf_size,
      .fh = fh,
      .offset = 0,
      .curr_file_index = 0,
      .rc = 0,
   };

   rc = fat_walk_directory(&walk_ctx,
                           dd->hdr,
                           dd->type,
                           NULL,
                           fat_get_first_cluster_generic(dd, fh->e),
                           fat_getdents64_cb,
                           &ctx); /* arg */

   if (rc != 0)
      return rc;

   if (ctx.rc != 0)
      return (int)ctx.rc;

   return (int) ctx.offset;
}

STATIC void fat_exclusive_lock(filesystem *fs)
{
   if (!(fs->flags & VFS_FS_RW))
      return; /* read-only: no lock is needed */

   NOT_IMPLEMENTED();
}

STATIC void fat_exclusive_unlock(filesystem *fs)
{
   if (!(fs->flags & VFS_FS_RW))
      return; /* read-only: no lock is needed */

   NOT_IMPLEMENTED();
}

STATIC void fat_shared_lock(filesystem *fs)
{
   if (!(fs->flags & VFS_FS_RW))
      return; /* read-only: no lock is needed */

   NOT_IMPLEMENTED();
}

STATIC void fat_shared_unlock(filesystem *fs)
{
   if (!(fs->flags & VFS_FS_RW))
      return; /* read-only: no lock is needed */

   NOT_IMPLEMENTED();
}

STATIC void fat_file_exlock(fs_handle h)
{
   filesystem *fs = get_fs(h);

   if (!(fs->flags & VFS_FS_RW))
      return; /* read-only: no lock is needed */

   NOT_IMPLEMENTED();
}

STATIC void fat_file_exunlock(fs_handle h)
{
   filesystem *fs = get_fs(h);

   if (!(fs->flags & VFS_FS_RW))
      return; /* read-only: no lock is needed */

   NOT_IMPLEMENTED();
}

STATIC void fat_file_shlock(fs_handle h)
{
   filesystem *fs = get_fs(h);

   if (!(fs->flags & VFS_FS_RW))
      return; /* read-only: no lock is needed */

   NOT_IMPLEMENTED();
}

STATIC void fat_file_shunlock(fs_handle h)
{
   filesystem *fs = get_fs(h);

   if (!(fs->flags & VFS_FS_RW))
      return; /* read-only: no lock is needed */

   NOT_IMPLEMENTED();
}

STATIC ssize_t fat_write(fs_handle h, char *buf, size_t len)
{
   filesystem *fs = get_fs(h);

   if (!(fs->flags & VFS_FS_RW))
      return -EBADF; /* read-only file system: can't write */

   NOT_IMPLEMENTED();
}

STATIC int fat_ioctl(fs_handle h, uptr request, void *arg)
{
   return -EINVAL;
}

STATIC int fat_fcntl(fs_handle h, int cmd, int arg)
{
   return -EINVAL;
}

static const file_ops static_ops_fat =
{
   .read = fat_read,
   .seek = fat_seek,
   .write = fat_write,
   .ioctl = fat_ioctl,
   .fcntl = fat_fcntl,

   .exlock = fat_file_exlock,
   .exunlock = fat_file_exunlock,
   .shlock = fat_file_shlock,
   .shunlock = fat_file_shunlock,
};

STATIC int
fat_open(vfs_path *p, fs_handle *out, int fl, mode_t mode)
{
   fat_file_handle *h;
   filesystem *fs = p->fs;
   fat_fs_path *fp = (fat_fs_path *)&p->fs_path;
   fat_entry *e = fp->entry;

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

   if (!(h = kzmalloc(sizeof(fat_file_handle))))
      return -ENOMEM;

   h->fs = fs;
   h->fops = &static_ops_fat;

   h->e = e;
   h->pos = 0;
   h->curr_cluster = fat_get_first_cluster(e);

   *out = h;
   return 0;
}

STATIC int fat_dup(fs_handle h, fs_handle *dup_h)
{
   fat_file_handle *new_h = kmalloc(sizeof(fat_file_handle));

   if (!new_h)
      return -ENOMEM;

   memcpy(new_h, h, sizeof(fat_file_handle));
   *dup_h = new_h;
   return 0;
}

static void
fat_get_entry(filesystem *fs,
              void *dir_inode,
              const char *name,
              ssize_t name_len,
              fs_path_struct *fs_path)
{
   fat_fs_device_data *d = fs->device_data;
   fat_fs_path *fp = (fat_fs_path *)fs_path;
   fat_entry *dir_entry;
   u32 dir_cluster;
   fat_search_ctx ctx;

   if (!dir_inode) {

      if (!name) {

         /* both dir_inode and name are NULL: getting a path to the root dir */

         *fp = (fat_fs_path) {
            .entry            = d->root_entry,
            .parent_entry     = d->root_entry,
            .unused           = NULL,
            .type             = VFS_DIR,
         };

         return;
      }

      /*
       * name is *not* NULL: that means that dir_inode is NULL just because
       * it refers to the root directory.
       */

      dir_entry = d->root_entry;
      dir_cluster = d->root_cluster;

   } else {

      /* dir_inode is VALID */
      dir_entry = dir_inode;
      dir_cluster = fat_get_first_cluster_generic(d, dir_entry);
   }

   /*
    * OK, now we have for sure exactly ONE of `dir_entry` and `dir_cluster`
    * set to a valid value. We can finally call fat_walk_directory().
    */

   if (dir_cluster) {

      /*
       * If we have a valid dir_cluster, we have to set dir_entry to NULL
       * because fat_walk_directory() expects exactly one of the `entry`
       * and `cluster` to be valid.
       */
      dir_entry = NULL;
   }

   fat_init_search_ctx(&ctx, name, true);
   fat_walk_directory(&ctx.walk_ctx, d->hdr, d->type, dir_entry, dir_cluster,
                      &fat_search_entry_cb, &ctx);

   fat_entry *res = !ctx.not_dir ? ctx.result : NULL;

   *fp = (fat_fs_path) {
      .entry         = res,
      .parent_entry  = NULL,
      .unused        = NULL,
      .type          = res ? (res->directory ? VFS_DIR : VFS_FILE) : VFS_NONE,
   };
}

static const fs_ops static_fsops_fat =
{
   .open2 = fat_open,
   .close = fat_close,
   .dup = fat_dup,
   .getdents64 = fat_getdents64,
   .unlink = NULL,
   .mkdir = NULL,
   .rmdir = NULL,
   .fstat = fat_stat64,
   .get_entry = fat_get_entry,

   .fs_exlock = fat_exclusive_lock,
   .fs_exunlock = fat_exclusive_unlock,
   .fs_shlock = fat_shared_lock,
   .fs_shunlock = fat_shared_unlock,
};

filesystem *fat_mount_ramdisk(void *vaddr, u32 flags)
{
   if (flags & VFS_FS_RW)
      panic("fat_mount_ramdisk: r/w mode is NOT currently supported");

   fat_fs_device_data *d = kmalloc(sizeof(fat_fs_device_data));

   if (!d)
      return NULL;

   d->hdr = (fat_header *) vaddr;
   d->type = fat_get_type(d->hdr);
   d->cluster_size = d->hdr->BPB_SecPerClus * d->hdr->BPB_BytsPerSec;
   d->root_entry = fat_get_rootdir(d->hdr, d->type, &d->root_cluster);

   filesystem *fs = kzmalloc(sizeof(filesystem));

   if (!fs) {
      kfree2(d, sizeof(fat_fs_device_data));
      return NULL;
   }

   fs->fs_type_name = "fat";
   fs->flags = flags;
   fs->device_id = vfs_get_new_device_id();
   fs->device_data = d;
   fs->fsops = &static_fsops_fat;

   return fs;
}

void fat_umount_ramdisk(filesystem *fs)
{
   kfree2(fs->device_data, sizeof(fat_fs_device_data));
   kfree2(fs, sizeof(filesystem));
}
