/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/errno.h>

#include "fs_int.h"

static u32 next_device_id;

/*
 * Returns:
 *  - 0 in case of non match.
 *  - strlen(mp) in case of a match
 */
STATIC int
check_mountpoint_match(const char *mp, u32 lm, const char *path, u32 lp)
{
   int m = 0;
   const u32 min_len = MIN(lm, lp);

   /*
    * Mount points MUST end with '/'.
    */
   ASSERT(mp[lm-1] == '/');

   for (size_t i = 0; i < min_len; i++) {

      if (mp[i] != path[i])
         break;

      m++;
   }

   /*
    * We assume that both the paths are absolute. Therefore, at least the
    * initial '/' must match.
    */
   ASSERT(m > 0);

   if (mp[m]) {

      if (mp[m] == '/' && !mp[m + 1] && !path[m]) {
         /* path is like '/dev' while mp is like '/dev/' */
         return m;
      }

      /*
       * The match stopped before the end of mount point's path.
       * Therefore, there is no match.
       */
      return 0;
   }

   if (path[m-1] != '/' && path[m-1] != 0) {

      /*
       * The match stopped before the end of a path component in 'path'.
       * In positive cases, the next character after a match (= position 'm')
       * is either a '/' or \0.
       */

      return 0;
   }

   return m;
}

int vfs_open(const char *path, fs_handle *out)
{
   mountpoint *mp, *best_match = NULL;
   int pl, rc, best_match_len = 0;
   const char *fs_path;
   mp_cursor cur;

   ASSERT(path != NULL);

   if (*path != '/')
      panic("vfs_open() works only with absolute paths");

   pl = strlen(path);

   mountpoint_iter_begin(&cur);

   while ((mp = mountpoint_get_next(&cur))) {

      int len = check_mountpoint_match(mp->path, mp->path_len, path, pl);

      if (len > best_match_len) {
         best_match = mp;
         best_match_len = len;
      }
   }

   if (!best_match) {
      rc = -ENOENT;
      goto out;
   }

   filesystem *fs = best_match->fs;

   vfs_fs_shlock(fs);
   {
      fs_path = (best_match_len < pl) ? path + best_match_len - 1 : "/";

      // printk("vfs_open('%s' as '%s' in '%s'@%u)\n",
      //        path, fs_path, fs->fs_type_name, fs->device_id);

      rc = fs->open(fs, fs_path, out);
   }
   vfs_fs_shunlock(fs);

out:
   mountpoint_iter_end(&cur);
   return rc;
}

void vfs_close(fs_handle h)
{
   fs_handle_base *hb = (fs_handle_base *) h;
   hb->fs->close(h);
}

int vfs_dup(fs_handle h, fs_handle *dup_h)
{
   fs_handle_base *hb = (fs_handle_base *) h;

   if (!hb)
      return -EBADF;

   int rc = hb->fs->dup(h, dup_h);

   if (rc)
      return rc;

   ASSERT(*dup_h != NULL);
   return 0;
}

ssize_t vfs_read(fs_handle h, void *buf, size_t buf_size)
{
   fs_handle_base *hb = (fs_handle_base *) h;
   ssize_t ret;

   if (!hb->fops.read)
      return -EINVAL;

   vfs_shlock(h);
   {
      ret = hb->fops.read(h, buf, buf_size);
   }
   vfs_shunlock(h);
   return ret;
}

ssize_t vfs_write(fs_handle h, void *buf, size_t buf_size)
{
   fs_handle_base *hb = (fs_handle_base *) h;
   ssize_t ret;

   if (!hb->fops.write)
      return -EINVAL;

   vfs_exlock(h);
   {
      ret = hb->fops.write(h, buf, buf_size);
   }
   vfs_exunlock(h);
   return ret;
}

off_t vfs_seek(fs_handle h, s64 off, int whence)
{
   fs_handle_base *hb = (fs_handle_base *) h;

   if (!hb->fops.seek)
      return -ESPIPE;

   // NOTE: this won't really work for big offsets in case off_t is 32-bit.
   return hb->fops.seek(h, off, whence);
}

int vfs_ioctl(fs_handle h, uptr request, void *argp)
{
   fs_handle_base *hb = (fs_handle_base *) h;
   int ret;

   if (!hb->fops.ioctl)
      return -ENOTTY; // Yes, ENOTTY *IS* the right error. See the man page.

   vfs_exlock(h);
   {
      ret = hb->fops.ioctl(h, request, argp);
   }
   vfs_exunlock(h);
   return ret;
}

int vfs_stat(fs_handle h, struct stat *statbuf)
{
   fs_handle_base *hb = (fs_handle_base *) h;
   int ret;

   ASSERT(hb->fops.stat != NULL); /* stat is NOT optional */

   vfs_shlock(h);
   {
      ret = hb->fops.stat(h, statbuf);
   }
   vfs_shunlock(h);
   return ret;
}

void vfs_exlock(fs_handle h)
{
   fs_handle_base *hb = (fs_handle_base *) h;
   ASSERT(hb != NULL);

   if (hb->fops.exlock) {
      hb->fops.exlock(h);
   } else {
      ASSERT(!hb->fops.exunlock);
      vfs_fs_exlock(get_fs(h));
   }
}

void vfs_exunlock(fs_handle h)
{
   fs_handle_base *hb = (fs_handle_base *) h;
   ASSERT(hb != NULL);

   if (hb->fops.exunlock) {
      hb->fops.exunlock(h);
   } else {
      ASSERT(!hb->fops.exlock);
      vfs_fs_exunlock(get_fs(h));
   }
}

void vfs_shlock(fs_handle h)
{
   fs_handle_base *hb = (fs_handle_base *) h;
   ASSERT(hb != NULL);

   if (hb->fops.shlock) {
      hb->fops.shlock(h);
   } else {
      ASSERT(!hb->fops.shunlock);
      vfs_fs_shlock(get_fs(h));
   }
}

void vfs_shunlock(fs_handle h)
{
   fs_handle_base *hb = (fs_handle_base *) h;
   ASSERT(hb != NULL);

   if (hb->fops.shunlock) {
      hb->fops.shunlock(h);
   } else {
      ASSERT(!hb->fops.shlock);
      vfs_fs_shunlock(get_fs(h));
   }
}

void vfs_fs_exlock(filesystem *fs)
{
   ASSERT(fs != NULL);
   ASSERT(fs->fs_exlock);

   fs->fs_exlock(fs);
}

void vfs_fs_exunlock(filesystem *fs)
{
   ASSERT(fs != NULL);
   ASSERT(fs->fs_exunlock);

   fs->fs_exunlock(fs);
}

void vfs_fs_shlock(filesystem *fs)
{
   ASSERT(fs != NULL);
   ASSERT(fs->fs_shlock);

   fs->fs_shlock(fs);
}

void vfs_fs_shunlock(filesystem *fs)
{
   ASSERT(fs != NULL);
   ASSERT(fs->fs_shunlock);

   fs->fs_shunlock(fs);
}

int vfs_getdents64(fs_handle h, struct linux_dirent64 *dirp, u32 buf_size)
{
   fs_handle_base *hb = (fs_handle_base *) h;
   int rc;

   ASSERT(hb != NULL);
   ASSERT(hb->fs->getdents64);

   vfs_fs_shlock(hb->fs);
   {
      // NOTE: the fs implementation MUST handle an invalid user 'dirp' pointer.
      rc = hb->fs->getdents64(h, dirp, buf_size);
   }
   vfs_fs_shunlock(hb->fs);
   return rc;
}

int vfs_fcntl(fs_handle h, int cmd, uptr arg)
{
   fs_handle_base *hb = (fs_handle_base *) h;
   int ret;

   if (!hb->fops.fcntl)
      return -EINVAL;

   vfs_exlock(h);
   {
      ret = hb->fops.fcntl(h, cmd, arg);
   }
   vfs_exunlock(h);
   return ret;
}

u32 vfs_get_new_device_id(void)
{
   return next_device_id++;
}
