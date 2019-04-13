/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/process.h>

#include "fs_int.h"

static u32 next_device_id;

/*
 * ----------------------------------------------------
 * VFS locking wrappers
 * ----------------------------------------------------
 */

void vfs_file_nolock(fs_handle h)
{
   /* do nothing */
}

void vfs_exlock(fs_handle h)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(h != NULL);

   fs_handle_base *hb = (fs_handle_base *) h;

   if (hb->fops.exlock) {
      hb->fops.exlock(h);
   } else {
      ASSERT(!hb->fops.exunlock);
      vfs_fs_exlock(get_fs(h));
   }
}

void vfs_exunlock(fs_handle h)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(h != NULL);

   fs_handle_base *hb = (fs_handle_base *) h;

   if (hb->fops.exunlock) {
      hb->fops.exunlock(h);
   } else {
      ASSERT(!hb->fops.exlock);
      vfs_fs_exunlock(get_fs(h));
   }
}

void vfs_shlock(fs_handle h)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(h != NULL);

   fs_handle_base *hb = (fs_handle_base *) h;

   if (hb->fops.shlock) {
      hb->fops.shlock(h);
   } else {
      ASSERT(!hb->fops.shunlock);
      vfs_fs_shlock(get_fs(h));
   }
}

void vfs_shunlock(fs_handle h)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(h != NULL);

   fs_handle_base *hb = (fs_handle_base *) h;

   if (hb->fops.shunlock) {
      hb->fops.shunlock(h);
   } else {
      ASSERT(!hb->fops.shlock);
      vfs_fs_shunlock(get_fs(h));
   }
}

void vfs_fs_exlock(filesystem *fs)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(fs != NULL);
   ASSERT(fs->fs_exlock);

   fs->fs_exlock(fs);
}

void vfs_fs_exunlock(filesystem *fs)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(fs != NULL);
   ASSERT(fs->fs_exunlock);

   fs->fs_exunlock(fs);
}

void vfs_fs_shlock(filesystem *fs)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(fs != NULL);
   ASSERT(fs->fs_shlock);

   fs->fs_shlock(fs);
}

void vfs_fs_shunlock(filesystem *fs)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(fs != NULL);
   ASSERT(fs->fs_shunlock);

   fs->fs_shunlock(fs);
}

/*
 * ----------------------------------------------------
 * Main VFS functions
 * ----------------------------------------------------
 */

int vfs_open(const char *path, fs_handle *out, int flags, mode_t mode)
{
   mountpoint *mp, *best_match = NULL;
   u32 len, pl, best_match_len = 0;
   const char *fs_path;
   mp_cursor cur;
   int rc;

   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(path != NULL);

   if (*path != '/')
      panic("vfs_open() works only with absolute paths");

   pl = (u32)strlen(path);
   mountpoint_iter_begin(&cur);

   while ((mp = mountpoint_get_next(&cur))) {

      len = mp_check_match(mp->path, mp->path_len, path, pl);

      if (len > best_match_len) {
         best_match = mp;
         best_match_len = len;
      }
   }

   if (!best_match) {
      mountpoint_iter_end(&cur);
      return -ENOENT;
   }

   filesystem *fs = best_match->fs;
   retain_obj(fs);                  // retain the FS and release the
   mountpoint_iter_end(&cur);       // mountpoint's lock

   fs_path = (best_match_len < pl) ? path + best_match_len - 1 : "/";

   /*
    * NOTE: we really DO NOT need to lock the whole FS in order to open/create
    * a file. At most, the directory where the file is/will be.
    *
    * TODO: make open() to NOT lock the whole FS.
    */
   if (flags & O_CREAT) {
      vfs_fs_exlock(fs);
      rc = fs->open(fs, fs_path, out, flags, mode);
      vfs_fs_exunlock(fs);
   } else {
      vfs_fs_shlock(fs);
      rc = fs->open(fs, fs_path, out, flags, mode);
      vfs_fs_shunlock(fs);
   }

   if (rc == 0) {

      /* open() succeeded, the FS is already retained */
      ((fs_handle_base *) *out)->fl_flags = flags;

      if (flags & O_CLOEXEC)
         ((fs_handle_base *) *out)->fd_flags |= FD_CLOEXEC;

   } else {
      /* open() failed, we need to release the FS */
      release_obj(fs);
   }

   return rc;
}

void vfs_close(fs_handle h)
{
   /*
    * TODO: consider forcing also vfs_close() to be run always with preemption
    * enabled. Reason: when one day when actual I/O devices will be supported,
    * close() might need in some cases to do some I/O.
    *
    * What prevents vfs_close() to run with preemption enabled is the function
    * terminate_process() which requires disabled preemption, because of its
    * (primitive) sync with signal handling.
    */
   ASSERT(h != NULL);

   fs_handle_base *hb = (fs_handle_base *) h;
   filesystem *fs = hb->fs;

#ifndef UNIT_TEST_ENVIRONMENT
   process_info *pi = get_curr_task()->pi;
   remove_all_mappings_of_handle(pi, h);
#endif

   hb->fs->close(h);
   release_obj(fs);

   /* while a filesystem is mounted, the minimum ref-count it can have is 1 */
   ASSERT(get_ref_count(fs) > 0);
}

int vfs_dup(fs_handle h, fs_handle *dup_h)
{
   ASSERT(h != NULL);

   fs_handle_base *hb = (fs_handle_base *) h;
   int rc;

   if (!hb)
      return -EBADF;

   if ((rc = hb->fs->dup(h, dup_h)))
      return rc;

   retain_obj(hb->fs);
   ASSERT(*dup_h != NULL);
   return 0;
}

ssize_t vfs_read(fs_handle h, void *buf, size_t buf_size)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(h != NULL);

   fs_handle_base *hb = (fs_handle_base *) h;
   ssize_t ret;

   if (!hb->fops.read)
      return -EBADF;

   if ((hb->fl_flags & O_WRONLY) && !(hb->fl_flags & O_RDWR))
      return -EBADF; /* file not opened for reading */

   vfs_shlock(h);
   {
      ret = hb->fops.read(h, buf, buf_size);
   }
   vfs_shunlock(h);
   return ret;
}

ssize_t vfs_write(fs_handle h, void *buf, size_t buf_size)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(h != NULL);

   fs_handle_base *hb = (fs_handle_base *) h;
   ssize_t ret;

   if (!hb->fops.write)
      return -EBADF;

   if (!(hb->fl_flags & (O_WRONLY | O_RDWR)))
      return -EBADF; /* file not opened for writing */

   vfs_exlock(h);
   {
      ret = hb->fops.write(h, buf, buf_size);
   }
   vfs_exunlock(h);
   return ret;
}

off_t vfs_seek(fs_handle h, s64 off, int whence)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(h != NULL);

   fs_handle_base *hb = (fs_handle_base *) h;

   if (!hb->fops.seek)
      return -ESPIPE;

   // NOTE: this won't really work for big offsets in case off_t is 32-bit.
   return hb->fops.seek(h, (off_t) off, whence);
}

int vfs_ioctl(fs_handle h, uptr request, void *argp)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(h != NULL);

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

int vfs_stat64(fs_handle h, struct stat64 *statbuf)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(h != NULL);

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

int vfs_getdents64(fs_handle h, struct linux_dirent64 *user_dirp, u32 buf_size)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   fs_handle_base *hb = (fs_handle_base *) h;
   int rc;

   ASSERT(hb != NULL);
   ASSERT(hb->fs->getdents64);

   vfs_fs_shlock(hb->fs);
   {
      // NOTE: the fs implementation MUST handle an invalid user 'dirp' pointer.
      rc = hb->fs->getdents64(h, user_dirp, buf_size);
   }
   vfs_fs_shunlock(hb->fs);
   return rc;
}

int vfs_fcntl(fs_handle h, int cmd, int arg)
{
   NO_TEST_ASSERT(is_preemption_enabled());
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

/*
 * ----------------------------------------------------
 * Ready-related VFS functions
 * ----------------------------------------------------
 */

bool vfs_read_ready(fs_handle h)
{
   fs_handle_base *hb = (fs_handle_base *) h;
   bool r;

   if (!hb->fops.read_ready)
      return true;

   vfs_shlock(h);
   {
      r = hb->fops.read_ready(h);
   }
   vfs_shunlock(h);
   return r;
}

bool vfs_write_ready(fs_handle h)
{
   fs_handle_base *hb = (fs_handle_base *) h;
   bool r;

   if (!hb->fops.write_ready)
      return true;

   vfs_shlock(h);
   {
      r = hb->fops.write_ready(h);
   }
   vfs_shunlock(h);
   return r;
}

bool vfs_except_ready(fs_handle h)
{
   fs_handle_base *hb = (fs_handle_base *) h;
   bool r;

   if (!hb->fops.except_ready)
      return false;

   vfs_shlock(h);
   {
      r = hb->fops.except_ready(h);
   }
   vfs_shunlock(h);
   return r;
}

kcond *vfs_get_rready_cond(fs_handle h)
{
   fs_handle_base *hb = (fs_handle_base *) h;

   if (!hb->fops.get_rready_cond)
      return NULL;

   return hb->fops.get_rready_cond(h);
}

kcond *vfs_get_wready_cond(fs_handle h)
{
   fs_handle_base *hb = (fs_handle_base *) h;

   if (!hb->fops.get_wready_cond)
      return NULL;

   return hb->fops.get_wready_cond(h);
}

kcond *vfs_get_except_cond(fs_handle h)
{
   fs_handle_base *hb = (fs_handle_base *) h;

   if (!hb->fops.get_except_cond)
      return NULL;

   return hb->fops.get_except_cond(h);
}
