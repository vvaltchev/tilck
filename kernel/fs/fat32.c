
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/fs/fat32.h>
#include <exos/fs/exvfs.h>
#include <exos/kmalloc.h>
#include <exos/errno.h>
#include <exos/datetime.h>

STATIC void fat_close(fs_handle handle)
{
   fat_file_handle *h = (fat_file_handle *)handle;
   kfree2(h, sizeof(fat_file_handle));
}

STATIC ssize_t fat_read(fs_handle handle,
                        char *buf,
                        size_t bufsize)
{
   fat_file_handle *h = (fat_file_handle *) handle;
   fat_fs_device_data *d = h->fs->device_data;
   u32 fsize = h->e->DIR_FileSize;
   u32 written_to_buf = 0;

   if (h->pos >= fsize) {

      /*
       * The cursor is at the end or past the end: nothing to read.
       */

      return 0;
   }

   do {

      char *data = fat_get_pointer_to_cluster_data(d->hdr, h->curr_cluster);

      const ssize_t file_rem = fsize - h->pos;
      const ssize_t buf_rem = bufsize - written_to_buf;
      const ssize_t cluster_offset = h->pos % d->cluster_size;
      const ssize_t cluster_rem = d->cluster_size - cluster_offset;
      const ssize_t to_read = MIN(cluster_rem, MIN(buf_rem, file_rem));

      memcpy(buf + written_to_buf, data + cluster_offset, to_read);
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

   return written_to_buf;
}


STATIC int fat_rewind(fs_handle handle)
{
   fat_file_handle *h = (fat_file_handle *) handle;
   h->pos = 0;
   h->curr_cluster = fat_get_first_cluster(h->e);
   return 0;
}

STATIC off_t fat_seek_forward(fs_handle handle,
                              off_t dist)
{
   fat_file_handle *h = (fat_file_handle *) handle;
   fat_fs_device_data *d = h->fs->device_data;
   u32 fsize = h->e->DIR_FileSize;
   ssize_t moved_distance = 0;

   if (dist == 0)
      return h->pos;

   if (h->pos + dist > fsize) {
      /* Allow, like Linux does, to seek past the end of a file. */
      h->pos += dist;
      h->curr_cluster = (u32) -1; /* invalid cluster */
      return h->pos;
   }

   do {

      const ssize_t file_rem = fsize - h->pos;
      const ssize_t dist_rem = dist - moved_distance;
      const ssize_t cluster_offset = h->pos % d->cluster_size;
      const ssize_t cluster_rem = d->cluster_size - cluster_offset;
      const ssize_t to_move = MIN(cluster_rem, MIN(dist_rem, file_rem));

      moved_distance += to_move;
      h->pos += to_move;

      if (to_move < cluster_rem) {
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

   return h->pos;
}

STATIC off_t fat_seek(fs_handle handle,
                      off_t off,
                      int whence)
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

STATIC int fat_stat(fs_handle h, struct stat *statbuf)
{
   fat_file_handle *fh = h;
   datetime_t crt_time, wrt_time;

   if (!h)
      return -ENOENT;

   bzero(statbuf, sizeof(struct stat));

   statbuf->st_dev = fh->fs->device_id;
   statbuf->st_ino = (ino_t)(uptr)&fh->e; /* use fat's entry as inode number */
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

STATIC int fat_getdents64(fs_handle h, struct linux_dirent64 *dirp, u32 buf_size)
{
   NOT_IMPLEMENTED();
}

STATIC void fat_exclusive_lock(filesystem *fs)
{
   if (!(fs->flags & EXVFS_FS_RW))
      return; /* read-only: no lock is needed */

   fat_fs_device_data *d = fs->device_data;
   kmutex_lock(&d->ex_mutex);
}

STATIC void fat_exclusive_unlock(filesystem *fs)
{
   if (!(fs->flags & EXVFS_FS_RW))
      return; /* read-only: no lock is needed */

   fat_fs_device_data *d = fs->device_data;
   kmutex_unlock(&d->ex_mutex);
}

STATIC void fat_shared_lock(filesystem *fs)
{
   if (!(fs->flags & EXVFS_FS_RW))
      return; /* read-only: no lock is needed */

   NOT_IMPLEMENTED();
}

STATIC void fat_shared_unlock(filesystem *fs)
{
   if (!(fs->flags & EXVFS_FS_RW))
      return; /* read-only: no lock is needed */

   NOT_IMPLEMENTED();
}

STATIC void fat_file_exlock(fs_handle h)
{
   // TODO: introduce a real per-file lock
   fat_exclusive_lock(get_fs(h));
}

STATIC void fat_file_exunlock(fs_handle h)
{
   // TODO: introduce a real per-file lock
   fat_exclusive_unlock(get_fs(h));
}

STATIC void fat_file_shlock(fs_handle h)
{
   // TODO: introduce a real per-file lock
   fat_shared_lock(get_fs(h));
}

STATIC void fat_file_shunlock(fs_handle h)
{
   // TODO: introduce a real per-file lock
   fat_shared_unlock(get_fs(h));
}

STATIC int fat_open(filesystem *fs, const char *path, fs_handle *out)
{
   fat_fs_device_data *d = (fat_fs_device_data *) fs->device_data;
   fat_entry *e = fat_search_entry(d->hdr, d->type, path);

   if (!e)
      return -ENOENT; /* file not found */

   fat_file_handle *h = kzmalloc(sizeof(fat_file_handle));

   if (!h)
      return -ENOMEM;

   h->fs = fs;
   h->fops.read = fat_read;
   h->fops.seek = fat_seek;
   h->fops.stat = fat_stat;
   h->fops.write = NULL;

   h->fops.exlock = fat_file_exlock;
   h->fops.exunlock = fat_file_exunlock;
   h->fops.shlock = fat_file_shlock;
   h->fops.shunlock = fat_file_shunlock;

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

filesystem *fat_mount_ramdisk(void *vaddr, u32 flags)
{
   if (flags & EXVFS_FS_RW)
      panic("fat_mount_ramdisk: r/w mode is NOT currently supported");

   fat_fs_device_data *d = kmalloc(sizeof(fat_fs_device_data));

   if (!d)
      return NULL;

   d->hdr = (fat_header *) vaddr;
   d->type = fat_get_type(d->hdr);
   d->cluster_size = d->hdr->BPB_SecPerClus * d->hdr->BPB_BytsPerSec;
   kmutex_init(&d->ex_mutex, KMUTEX_FL_RECURSIVE);

   filesystem *fs = kzmalloc(sizeof(filesystem));

   if (!fs) {
      kfree2(d, sizeof(fat_fs_device_data));
      return NULL;
   }

   fs->flags = flags;
   fs->device_id = exvfs_get_new_device_id();
   fs->device_data = d;
   fs->open = fat_open;
   fs->close = fat_close;
   fs->dup = fat_dup;
   fs->getdents64 = fat_getdents64;

   fs->fs_exlock = fat_exclusive_lock;
   fs->fs_exunlock = fat_exclusive_unlock;
   fs->fs_shlock = fat_shared_lock;
   fs->fs_shunlock = fat_shared_unlock;
   return fs;
}

void fat_umount_ramdisk(filesystem *fs)
{
   fat_fs_device_data *d = fs->device_data;

   kmutex_destroy(&d->ex_mutex);
   kfree2(fs->device_data, sizeof(fat_fs_device_data));
   kfree2(fs, sizeof(filesystem));
}
