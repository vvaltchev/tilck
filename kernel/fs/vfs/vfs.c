/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/process_mm.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/debug_utils.h>

#include <dirent.h> // system header

#include "../fs_int.h"
#include "vfs_mp2.c.h"
#include "vfs_locking.c.h"
#include "vfs_resolve.c.h"
#include "vfs_getdents.c.h"
#include "vfs_op_ready.c.h"

static u32 next_device_id;

/* ------------ handle-based functions ------------- */

void vfs_close2(process_info *pi, fs_handle h)
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
   remove_all_mappings_of_handle(pi, h);
#endif

   fs->fsops->close(h);
   release_obj(fs);

   /* while a filesystem is mounted, the minimum ref-count it can have is 1 */
   ASSERT(get_ref_count(fs) > 0);
}

void vfs_close(fs_handle h)
{
   vfs_close2(get_curr_task()->pi, h);
}

int vfs_dup(fs_handle h, fs_handle *dup_h)
{
   ASSERT(h != NULL);

   fs_handle_base *hb = (fs_handle_base *) h;
   int rc;

   if (!hb)
      return -EBADF;

   if ((rc = hb->fs->fsops->dup(h, dup_h)))
      return rc;

   /* The new file descriptor does NOT share old file descriptor's fd_flags */
   ((fs_handle_base*) *dup_h)->fd_flags = 0;

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

   if (!hb->fops->read)
      return -EBADF;

   if ((hb->fl_flags & O_WRONLY) && !(hb->fl_flags & O_RDWR))
      return -EBADF; /* file not opened for reading */

   vfs_shlock(h);
   {
      ret = hb->fops->read(h, buf, buf_size);
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

   if (!hb->fops->write)
      return -EBADF;

   if (!(hb->fl_flags & (O_WRONLY | O_RDWR)))
      return -EBADF; /* file not opened for writing */

   vfs_exlock(h);
   {
      ret = hb->fops->write(h, buf, buf_size);
   }
   vfs_exunlock(h);
   return ret;
}

offt vfs_seek(fs_handle h, s64 off, int whence)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(h != NULL);
   offt ret;

   if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END)
      return -EINVAL; /* Tilck does NOT support SEEK_DATA and SEEK_HOLE */

   fs_handle_base *hb = (fs_handle_base *) h;

   if (!hb->fops->seek)
      return -ESPIPE;

   vfs_shlock(h);
   {
      // NOTE: this won't really work for big offsets in case offt is 32-bit.
      ret = hb->fops->seek(h, (offt) off, whence);
   }
   vfs_shunlock(h);
   return ret;
}

int vfs_ioctl(fs_handle h, uptr request, void *argp)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(h != NULL);

   fs_handle_base *hb = (fs_handle_base *) h;
   int ret;

   if (!hb->fops->ioctl)
      return -ENOTTY; // Yes, ENOTTY *IS* the right error. See the man page.

   vfs_exlock(h);
   {
      ret = hb->fops->ioctl(h, request, argp);
   }
   vfs_exunlock(h);
   return ret;
}

int vfs_fcntl(fs_handle h, int cmd, int arg)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   fs_handle_base *hb = (fs_handle_base *) h;
   int ret;

   if (!hb->fops->fcntl)
      return -EINVAL;

   vfs_exlock(h);
   {
      ret = hb->fops->fcntl(h, cmd, arg);
   }
   vfs_exunlock(h);
   return ret;
}

int vfs_ftruncate(fs_handle h, offt length)
{
   fs_handle_base *hb = (fs_handle_base *) h;
   const fs_ops *fsops = hb->fs->fsops;

   if (!fsops->truncate)
      return -EROFS;

   return fsops->truncate(hb->fs, fsops->get_inode(h), length);
}

int vfs_fstat64(fs_handle h, struct stat64 *statbuf)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(h != NULL);

   fs_handle_base *hb = (fs_handle_base *) h;
   filesystem *fs = hb->fs;
   const fs_ops *fsops = fs->fsops;
   int ret;

   vfs_shlock(h);
   {
      ret = fsops->stat(fs, fsops->get_inode(h), statbuf);
   }
   vfs_shunlock(h);
   return ret;
}

/* ----------- path-based functions -------------- */

typedef int (*vfs_func_impl)(filesystem *, vfs_path *, uptr, uptr, uptr);

static ALWAYS_INLINE int
__vfs_path_funcs_wrapper(const char *path,
                         bool exlock,
                         bool res_last_sl,
                         vfs_func_impl func,
                         uptr a1, uptr a2, uptr a3)
{
   vfs_path p;
   int rc;

   NO_TEST_ASSERT(is_preemption_enabled());

   if ((rc = vfs_resolve(path, &p, exlock, res_last_sl)) < 0)
      return rc;

   ASSERT(p.fs != NULL);
   rc = func(p.fs, &p, a1, a2, a3);

   vfs_smart_fs_unlock(p.fs, exlock);
   release_obj(p.fs);
   return rc;
}

#define vfs_path_funcs_wrapper(path, exlock, rsl, func, a1, a2, a3)           \
   __vfs_path_funcs_wrapper(path,                                             \
                            exlock,                                           \
                            rsl,                                              \
                            (vfs_func_impl)func,                              \
                            (uptr)a1, (uptr)a2, (uptr)a3)

static ALWAYS_INLINE int
vfs_open_impl(filesystem *fs, vfs_path *p,
              fs_handle *out, int flags, mode_t mode)
{
   int rc;

   if ((rc = fs->fsops->open(p, out, flags, mode)))
      return rc;

   /* open() succeeded, the FS is already retained */
   ((fs_handle_base *) *out)->fl_flags = flags;

   if (flags & O_CLOEXEC)
      ((fs_handle_base *) *out)->fd_flags |= FD_CLOEXEC;

   /* file handles retain their filesystem */
   retain_obj(fs);
   return 0;
}

int vfs_open(const char *path, fs_handle *out, int flags, mode_t mode)
{
   if (flags & O_ASYNC)
      return -EINVAL; /* TODO: Tilck does not support ASYNC I/O yet */

   if ((flags & O_TMPFILE) == O_TMPFILE)
      return -EOPNOTSUPP; /* TODO: Tilck does not support O_TMPFILE yet */

   return vfs_path_funcs_wrapper(
      path,
      true,             /* exlock */
      true,             /* res_last_sl */
      &vfs_open_impl,
      out,
      flags,
      mode
   );
}

static ALWAYS_INLINE int
vfs_stat64_impl(filesystem *fs,
                vfs_path *p,
                struct stat64 *statbuf,
                bool res_last_sl,
                uptr unused1)
{
   if (!p->fs_path.inode)
      return -ENOENT;

   return fs->fsops->stat(fs, p->fs_path.inode, statbuf);
}

int vfs_stat64(const char *path, struct stat64 *statbuf, bool res_last_sl)
{
   return vfs_path_funcs_wrapper(
      path,
      false,               /* exlock */
      res_last_sl,         /* res_last_sl */
      &vfs_stat64_impl,
      statbuf,
      res_last_sl,
      0
   );
}

static ALWAYS_INLINE int
vfs_mkdir_impl(filesystem *fs, vfs_path *p, mode_t mode, uptr u1, uptr u2)
{
   if (!fs->fsops->mkdir)
      return -EPERM;

   if (!(fs->flags & VFS_FS_RW))
      return -EROFS;

   if (p->fs_path.inode)
      return -EEXIST;

   return fs->fsops->mkdir(p, mode);
}

int vfs_mkdir(const char *path, mode_t mode)
{
   return vfs_path_funcs_wrapper(
      path,
      true,             /* exlock */
      false,            /* res_last_sl */
      vfs_mkdir_impl,
      mode,
      0,
      0
   );
}

static ALWAYS_INLINE int
vfs_rmdir_impl(filesystem *fs, vfs_path *p, uptr u1, uptr u2, uptr u3)
{
   if (!fs->fsops->rmdir)
      return -EPERM;

   if (!(fs->flags & VFS_FS_RW))
      return -EROFS;

   if (!p->fs_path.inode)
      return -ENOENT;

   return fs->fsops->rmdir(p);
}

int vfs_rmdir(const char *path)
{
   return vfs_path_funcs_wrapper(
      path,
      true,             /* exlock */
      false,            /* res_last_sl */
      vfs_rmdir_impl,
      0, 0, 0
   );
}

static ALWAYS_INLINE int
vfs_unlink_impl(filesystem *fs, vfs_path *p, uptr u1, uptr u2, uptr u3)
{
   if (!fs->fsops->unlink)
      return -EPERM;

   if (!(fs->flags & VFS_FS_RW))
      return -EROFS;

   if (!p->fs_path.inode)
      return -ENOENT;

   return fs->fsops->unlink(p);
}

int vfs_unlink(const char *path)
{
   return vfs_path_funcs_wrapper(
      path,
      true,             /* exlock */
      false,            /* res_last_sl */
      vfs_unlink_impl,
      0, 0, 0
   );
}

static ALWAYS_INLINE int
vfs_truncate_impl(filesystem *fs, vfs_path *p, offt len, uptr u1, uptr u2)
{
   if (!fs->fsops->truncate)
      return -EROFS;

   if (!(fs->flags & VFS_FS_RW))
      return -EROFS;

   if (!p->fs_path.inode)
      return -ENOENT;

   return fs->fsops->truncate(fs, p->fs_path.inode, len);
}

int vfs_truncate(const char *path, offt len)
{
   return vfs_path_funcs_wrapper(
      path,
      false,               /* exlock */
      true,                /* res_last_sl */
      vfs_truncate_impl,
      len,
      0, 0
   );
}

static ALWAYS_INLINE int
vfs_symlink_impl(filesystem *fs,
                 vfs_path *p, const char *target, uptr u1, uptr u2)
{
   if (!fs->fsops->symlink)
      return -EPERM;

   if (!(fs->flags & VFS_FS_RW))
      return -EROFS;

   if (p->fs_path.inode)
      return -EEXIST; /* the linkpath already exists! */

   return fs->fsops->symlink(target, p);
}

int vfs_symlink(const char *target, const char *linkpath)
{
   return vfs_path_funcs_wrapper(
      linkpath,
      true,             /* exlock */
      false,            /* res_last_sl */
      vfs_symlink_impl,
      target,
      0, 0
   );
}

static ALWAYS_INLINE int
vfs_readlink_impl(filesystem *fs, vfs_path *p, char *buf, uptr u1, uptr u2)
{
   if (!fs->fsops->readlink) {
      /*
       * If there's no readlink(), symlinks are not supported by the FS, ergo
       * the last component of `path` cannot be referring to a symlink.
       */
      return -EINVAL;
   }

   if (!p->fs_path.inode)
      return -ENOENT;

   return fs->fsops->readlink(p, buf);
}

/* NOTE: `buf` is guaranteed to have room for at least MAX_PATH chars */
int vfs_readlink(const char *path, char *buf)
{
   return vfs_path_funcs_wrapper(
      path,
      false,               /* exlock */
      false,               /* res_last_sl */
      vfs_readlink_impl,
      buf,
      0, 0
   );
}

static ALWAYS_INLINE int
vfs_chown_impl(filesystem *fs, vfs_path *p, int owner, int group, bool reslink)
{
   return (owner == 0 && group == 0) ? 0 : -EPERM;
}

int vfs_chown(const char *path, int owner, int group, bool reslink)
{
   return vfs_path_funcs_wrapper(
      path,
      false,            /* exlock */
      reslink,          /* res_last_sl */
      vfs_chown_impl,
      owner,
      group,
      reslink
   );
}

static ALWAYS_INLINE int
vfs_chmod_impl(filesystem *fs, vfs_path *p, mode_t mode)
{
   if (!fs->fsops->chmod)
      return -EPERM;

   if (!(fs->flags & VFS_FS_RW))
      return -EROFS;

   if (!p->fs_path.inode)
      return -ENOENT;

   return fs->fsops->chmod(fs, p->fs_path.inode, mode);
}

int vfs_chmod(const char *path, mode_t mode)
{
   return vfs_path_funcs_wrapper(
      path,
      false,            /* exlock */
      true,             /* res_last_sl */
      vfs_chmod_impl,
      mode,
      0, 0
   );
}

static int
vfs_rename_or_link(const char *oldpath,
                   const char *newpath,
                   func_2paths (*get_func_ptr)(filesystem *))
{
   filesystem *fs;
   func_2paths func;
   vfs_path oldp, newp;
   int rc;

   NO_TEST_ASSERT(is_preemption_enabled());

   /* First, just resolve the old path using a shared lock */
   if ((rc = vfs_resolve(oldpath, &oldp, false, false)) < 0)
      return rc;

   ASSERT(oldp.fs != NULL);
   fs = oldp.fs;

   if (!oldp.fs_path.inode) {

      /* The old path does not exist */
      vfs_smart_fs_unlock(fs, false);
      release_obj(fs);
      return -ENOENT;
   }

   /* Everything was fine: now retain the file and release the lock */
   vfs_retain_inode_at(&oldp);
   vfs_smart_fs_unlock(fs, false);

   /* Now, resolve the new path grabbing an exclusive lock */
   if ((rc = vfs_resolve(newpath, &newp, true, false)) < 0) {

      /*
       * Oops, something when wrong: release the oldpath's inode and fs.
       * Note: no need for release anything about the new path since the func
       * already does that in the error cases.
       */
      vfs_release_inode_at(&oldp);
      release_obj(fs);
      return rc;
   }

   ASSERT(newp.fs != NULL);

   /*
    * OK, now we're at a crucial point: check if the two files belong to the
    * same filesystem.
    */

   if (newp.fs != fs) {

      /*
       * They do *not* belong to the same fs. It's impossible to continue.
       * We have to release: the exlock and the retain count of the new
       * fs plus the retain count of old's inode and its filesystem.
       */

      vfs_smart_fs_unlock(newp.fs, true);
      release_obj(newp.fs);

      vfs_release_inode_at(&oldp);
      release_obj(fs);
      return -EXDEV;
   }

   /*
    * Great! They *do* belong to the same fs. Now we have to just release one
    * fs retain count and the old inode's retain count as well.
    */

   release_obj(fs);
   vfs_release_inode_at(&oldp); /* note: we're still holding an exlock on fs */

   /* Finally, we can call filesystem's func (if any) */
   func = get_func_ptr(fs);

   rc = func
      ? fs->flags & VFS_FS_RW
         ? func(fs, &oldp, &newp)
         : -EROFS /* read-only filesystem */
      : -EPERM; /* not supported */

   /* We're done, release fs's exlock and its retain count */
   vfs_smart_fs_unlock(fs, true);
   release_obj(fs);
   return rc;
}

static ALWAYS_INLINE func_2paths vfs_get_rename_func(filesystem *fs)
{
   return fs->fsops->rename;
}

static ALWAYS_INLINE func_2paths vfs_get_link_func(filesystem *fs)
{
   return fs->fsops->link;
}

int vfs_rename(const char *oldpath, const char *newpath)
{
   return vfs_rename_or_link(oldpath, newpath, &vfs_get_rename_func);
}

int vfs_link(const char *oldpath, const char *newpath)
{
   return vfs_rename_or_link(oldpath, newpath, &vfs_get_link_func);
}

int vfs_fchmod(fs_handle h, mode_t mode)
{
   fs_handle_base *hb = h;
   const fs_ops *fsops = hb->fs->fsops;
   return fsops->chmod(hb->fs, fsops->get_inode(h), mode);
}

int vfs_mmap(user_mapping *um, bool register_only)
{
   fs_handle_base *hb = um->h;
   const file_ops *fops = hb->fops;

   if (!fops->mmap)
      return -ENODEV;

   ASSERT(fops->munmap != NULL);
   return fops->mmap(um, register_only);
}

int vfs_munmap(fs_handle h, void *vaddr, size_t len)
{
   fs_handle_base *hb = h;
   const file_ops *fops = hb->fops;

   if (!fops->munmap)
      return -ENODEV;

   ASSERT(fops->mmap != NULL);
   return fops->munmap(h, vaddr, len);
}

bool vfs_handle_fault(fs_handle h, void *va, bool p, bool rw)
{
   fs_handle_base *hb = h;
   const file_ops *fops = hb->fops;

   if (!fops->handle_fault)
      return false;

   return fops->handle_fault(h, va, p, rw);
}

u32 vfs_get_new_device_id(void)
{
   return next_device_id++;
}
