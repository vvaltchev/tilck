/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/process_mm.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/debug_utils.h>

#include <dirent.h> // system header

#include "../fs_int.h"
#include "vfs_mp.c.h"
#include "vfs_locking.c.h"
#include "vfs_resolve.c.h"
#include "vfs_getdents.c.h"
#include "vfs_op_ready.c.h"

static u32 next_device_id;

void
vfs_init_fs_handle_base_fields(struct fs_handle_base *hb,
                               struct fs *fs,
                               const struct file_ops *fops)
{
   hb->pi = get_curr_proc();
   hb->fs = fs;
   hb->fops = fops;
}


/* ------------ handle-based functions ------------- */

void vfs_close2(struct process *pi, fs_handle h)
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

   struct fs_handle_base *hb = (struct fs_handle_base *) h;
   struct fs *fs = hb->fs;

   if (!pi->vforked) {
      remove_all_mappings_of_handle(pi, h);
   }

   fs->fsops->close(h);
   release_obj(fs);

   /* while a struct fs is mounted, the minimum ref-count it can have is 1 */
   ASSERT(get_ref_count(fs) > 0);
}

void vfs_close(fs_handle h)
{
   vfs_close2(get_curr_proc(), h);
}

/*
 * Note: because of the way file handles are allocated on Tilck, dup() can
 * fail with -ENOMEM, while on POSIX systems that is not allowed.
 *
 * TODO: fix the file-handles allocation mechanism in order to make impossible
 * dup() failing with -ENOMEM, by substantially pre-allocating the memory for
 * the file handle objects.
 */
int vfs_dup(fs_handle h, fs_handle *dup_h)
{
   ASSERT(h != NULL);

   struct fs_handle_base *hb = (struct fs_handle_base *) h;
   int rc;

   if (!hb)
      return -EBADF;

   if ((rc = hb->fs->fsops->dup(h, dup_h)))
      return rc;

   /* The new file descriptor does NOT share old file descriptor's fd_flags */
   ((struct fs_handle_base*) *dup_h)->fd_flags = 0;

   retain_obj(hb->fs);
   ASSERT(*dup_h != NULL);
   return 0;
}

ssize_t vfs_read(fs_handle h, void *buf, size_t buf_size)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(h != NULL);

   struct fs_handle_base *hb = (struct fs_handle_base *) h;

   if (!hb->fops->read)
      return -EBADF;

   if ((hb->fl_flags & O_WRONLY) && !(hb->fl_flags & O_RDWR))
      return -EBADF; /* file not opened for reading */

   return hb->fops->read(h, buf, buf_size);
}

ssize_t vfs_write(fs_handle h, void *buf, size_t buf_size)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(h != NULL);

   struct fs_handle_base *hb = (struct fs_handle_base *) h;

   if (!hb->fops->write)
      return -EBADF;

   if (!(hb->fl_flags & (O_WRONLY | O_RDWR)))
      return -EBADF; /* file not opened for writing */

   return hb->fops->write(h, buf, buf_size);
}

offt vfs_seek(fs_handle h, s64 off, int whence)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(h != NULL);

   if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END)
      return -EINVAL; /* Tilck does NOT support SEEK_DATA and SEEK_HOLE */

   struct fs_handle_base *hb = (struct fs_handle_base *) h;

   if (!hb->fops->seek)
      return -ESPIPE;

   // NOTE: this won't really work for big offsets in case offt is 32-bit.
   return hb->fops->seek(h, (offt) off, whence);
}

int vfs_ioctl(fs_handle h, ulong request, void *argp)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(h != NULL);

   struct fs_handle_base *hb = (struct fs_handle_base *) h;

   if (!hb->fops->ioctl)
      return -ENOTTY; // Yes, ENOTTY *IS* the right error. See the man page.

   return hb->fops->ioctl(h, request, argp);
}

int vfs_ftruncate(fs_handle h, offt length)
{
   struct fs_handle_base *hb = (struct fs_handle_base *) h;
   const struct fs_ops *fsops = hb->fs->fsops;

   if (!fsops->truncate)
      return -EROFS;

   return fsops->truncate(hb->fs, fsops->get_inode(h), length);
}

int vfs_fstat64(fs_handle h, struct stat64 *statbuf)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(h != NULL);

   struct fs_handle_base *hb = (struct fs_handle_base *) h;
   struct fs *fs = hb->fs;
   const struct fs_ops *fsops = fs->fsops;

   return fsops->stat(fs, fsops->get_inode(h), statbuf);
}

/* ----------- path-based functions -------------- */

typedef int (*vfs_func_impl)(struct fs*, struct vfs_path*, ulong, ulong, ulong);

static ALWAYS_INLINE int
__vfs_path_funcs_wrapper(const char *path,
                         bool exlock,
                         bool res_last_sl,
                         vfs_func_impl func,
                         ulong a1, ulong a2, ulong a3)
{
   struct vfs_path p;
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
                            (vfs_func_impl)(void *)func,                      \
                            (ulong)a1, (ulong)a2, (ulong)a3)

static ALWAYS_INLINE int
vfs_open_impl(struct fs *fs, struct vfs_path *p,
              fs_handle *out, int flags, mode_t mode)
{
   int rc;

   if (flags & O_DIRECTORY) {
      if (p->fs_path.type != VFS_DIR)
         return -ENOTDIR;
   }

   if ((rc = fs->fsops->open(p, out, flags, mode)))
      return rc;

   /* open() succeeded, the FS is already retained */
   ((struct fs_handle_base *) *out)->fl_flags = flags;

   if (flags & O_CLOEXEC)
      ((struct fs_handle_base *) *out)->fd_flags |= FD_CLOEXEC;

   /* file handles retain their struct fs */
   retain_obj(fs);
   return 0;
}

int vfs_open(const char *path, fs_handle *out, int flags, mode_t mode)
{
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
vfs_stat64_impl(struct fs *fs,
                struct vfs_path *p,
                struct stat64 *statbuf,
                bool res_last_sl,
                ulong unused1)
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
vfs_mkdir_impl(struct fs *fs, struct vfs_path *p, mode_t mode, ulong x, ulong y)
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
vfs_rmdir_impl(struct fs *fs, struct vfs_path *p, ulong u1, ulong u2, ulong u3)
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
vfs_unlink_impl(struct fs *fs, struct vfs_path *p, ulong u1, ulong u2, ulong u3)
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
vfs_truncate_impl(struct fs *fs, struct vfs_path *p, offt len, ulong x, ulong y)
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
vfs_symlink_impl(struct fs *fs,
                 struct vfs_path *p, const char *target, ulong u1, ulong u2)
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
vfs_readlink_impl(struct fs *fs,
                  struct vfs_path *p,
                  char *buf,
                  ulong u1,
                  ulong u2)
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
vfs_chown_impl(struct fs *fs,
               struct vfs_path *p,
               int owner,
               int group,
               bool reslink)
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
vfs_chmod_impl(struct fs *fs, struct vfs_path *p, mode_t mode)
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

static ALWAYS_INLINE int
vfs_utimens_impl(struct fs *fs,
                 struct vfs_path *p,
                 const struct k_timespec64 ts[2])
{
   if (!fs->fsops->futimens)
      return -EROFS;

   return fs->fsops->futimens(fs, p->fs_path.inode, ts);
}

int vfs_utimens(const char *path, const struct k_timespec64 times[2])
{
   return vfs_path_funcs_wrapper(
      path,
      true,            /* exlock */
      true,            /* res_last_sl */
      vfs_utimens_impl,
      times,
      0, 0
   );
}

static int
vfs_rename_or_link(const char *oldpath,
                   const char *newpath,
                   func_2paths (*get_func_ptr)(struct fs *))
{
   struct fs *fs;
   func_2paths func;
   struct vfs_path oldp, newp;
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
    * same struct fs.
    */

   if (newp.fs != fs) {

      /*
       * They do *not* belong to the same fs. It's impossible to continue.
       * We have to release: the exlock and the retain count of the new
       * fs plus the retain count of old's inode and its struct fs.
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

   /* Finally, we can call struct fs's func (if any) */
   func = get_func_ptr(fs);

   rc = func
      ? fs->flags & VFS_FS_RW
         ? func(fs, &oldp, &newp)
         : -EROFS /* read-only struct fs */
      : -EPERM; /* not supported */

   /* We're done, release fs's exlock and its retain count */
   vfs_smart_fs_unlock(fs, true);
   release_obj(fs);
   return rc;
}

static ALWAYS_INLINE func_2paths vfs_get_rename_func(struct fs *fs)
{
   return fs->fsops->rename;
}

static ALWAYS_INLINE func_2paths vfs_get_link_func(struct fs *fs)
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
   struct fs_handle_base *hb = h;
   const struct fs_ops *fsops = hb->fs->fsops;
   return fsops->chmod(hb->fs, fsops->get_inode(h), mode);
}

int vfs_mmap(struct user_mapping *um, pdir_t *pdir, int flags)
{
   struct fs_handle_base *hb = um->h;
   const struct file_ops *fops = hb->fops;

   if (!fops->mmap)
      return -ENODEV;

   ASSERT(fops->munmap != NULL);
   return fops->mmap(um, pdir, flags);
}

int vfs_munmap(fs_handle h, void *vaddr, size_t len)
{
   struct fs_handle_base *hb = h;
   const struct file_ops *fops = hb->fops;

   if (!fops->munmap)
      return -ENODEV;

   ASSERT(fops->mmap != NULL);
   return fops->munmap(h, vaddr, len);
}

bool vfs_handle_fault(fs_handle h, void *va, bool p, bool rw)
{
   struct fs_handle_base *hb = h;
   const struct file_ops *fops = hb->fops;

   if (!fops->handle_fault)
      return false;

   return fops->handle_fault(h, va, p, rw);
}

int vfs_futimens(fs_handle h, const struct k_timespec64 times[2])
{
   struct fs_handle_base *hb = h;
   const struct fs_ops *fsops = hb->fs->fsops;

   if (!fsops->futimens)
      return -EROFS;

   return fsops->futimens(hb->fs, fsops->get_inode(h), times);
}

ssize_t vfs_readv(fs_handle h, const struct iovec *iov, int iovcnt)
{
   struct fs_handle_base *hb = h;
   struct task *curr = get_curr_task();
   ssize_t ret = 0;
   ssize_t rc;
   size_t len;

   if (hb->fops->readv)
      return hb->fops->readv(h, iov, iovcnt);

   /*
    * readv() is not implemented in the file system: implement here it in a
    * generic but non-atomic way. There's nothing more we can do. Also, the
    * POSIX standard does not require readv() to be atomic:
    *
    *    https://pubs.opengroup.org/onlinepubs/9699919799/
    *
    * Note: Linux's man page claims that readv/writev must be atomic: that's
    * possible now because all of the Linux file systems support internally the
    * scatter/gather I/O. On Tilck, not all the file systems will support it.
    */

   for (int i = 0; i < iovcnt; i++) {

      len = MIN(iov[i].iov_len, IO_COPYBUF_SIZE);

      rc = vfs_read(h, curr->io_copybuf, len);

      if (rc < 0) {
         ret = rc;
         break;
      }

      if (copy_to_user(iov[i].iov_base, curr->io_copybuf, len))
         return -EFAULT;

      ret += rc;

      if (rc < (ssize_t)iov[i].iov_len)
         break; // Not enough data to fill all the user buffers.
   }

   return ret;
}

ssize_t vfs_writev(fs_handle h, const struct iovec *iov, int iovcnt)
{
   struct fs_handle_base *hb = h;
   struct task *curr = get_curr_task();
   ssize_t ret = 0;
   ssize_t rc;
   size_t len;

   if (hb->fops->writev)
      return hb->fops->writev(h, iov, iovcnt);

   /*
    * writev() is not implemented in the file system: implement here it in a
    * generic but non-atomic way. There's nothing more we can do. Also, the
    * POSIX standard does not require writev() to be atomic:
    *
    *    https://pubs.opengroup.org/onlinepubs/9699919799/
    *
    * Note: Linux's man page claims that readv/writev must be atomic: that's
    * possible now because all of the Linux file systems support internally the
    * scatter/gather I/O. On Tilck, not all the file systems will support it.
    */

   for (int i = 0; i < iovcnt; i++) {

      len = MIN(iov[i].iov_len, IO_COPYBUF_SIZE);

      if (copy_from_user(curr->io_copybuf, iov[i].iov_base, len))
         return -EFAULT;

      rc = vfs_write(h, curr->io_copybuf, len);

      if (rc < 0) {
         ret = rc;
         break;
      }

      ret += rc;

      if (rc < (ssize_t)iov[i].iov_len) {
         // For some reason (perfectly legit) we couldn't write the whole
         // user data (i.e. network card's buffers are full).
         break;
      }
   }

   return ret;
}

u32 vfs_get_new_device_id(void)
{
   return next_device_id++;
}

struct fs *create_fs_obj(const char *type)
{
   struct fs *fs = kzmalloc(sizeof(struct fs));

   if (!fs)
      return NULL;

   fs->pss_lock_root = NULL;
   fs->fs_type_name = type;
   return fs;
}

void destory_fs_obj(struct fs *fs)
{
   ASSERT(!fs->pss_lock_root);
   kfree2(fs, sizeof(struct fs));
}

int vfs_exlock_noblock(struct fs *fs, vfs_inode_ptr_t i)
{
   int rc;

   if (!(fs->flags & VFS_FS_RW))
      return 0; /* always succeed for read-only mounted filesystems */

   if (!fs->fsops->exlock_noblk) {
      /* Make sure its counterpart function neither exists */
      ASSERT(!fs->fsops->exunlock);
      return -ENOLCK;
   }

   /* Make sure its counterpart function do exists as well */
   ASSERT(fs->fsops->exunlock != NULL);

   /* Retain the fs, the inode and finally try to grab the exlock */
   retain_obj(fs);
   vfs_retain_inode(fs, i);
   rc = fs->fsops->exlock_noblk(fs, i);

   if (rc != 0) {
      /* error case: we must release the inode and the fs obj */
      vfs_release_inode(fs, i);
      release_obj(fs);
   }

   return rc;
}

int vfs_exunlock(struct fs *fs, vfs_inode_ptr_t i)
{
   int rc;

   if (!(fs->flags & VFS_FS_RW))
      return 0; /* always succeed for read-only mounted filesystems */

   if (!fs->fsops->exunlock) {
      /* Make sure its counterpart function neither exists */
      ASSERT(!fs->fsops->exlock_noblk);
      return -ENOLCK;
   }

   /* Make sure its counterpart function do exists as well */
   ASSERT(fs->fsops->exlock_noblk != NULL);
   rc = fs->fsops->exunlock(fs, i);

   if (!rc) {
      /* success case: release the inode and the fs obj */
      vfs_release_inode(fs, i);
      release_obj(fs);
   }

   return rc;
}
