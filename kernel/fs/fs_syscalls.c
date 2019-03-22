/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/fault_resumable.h>

#include <fcntl.h>      // system header

static inline bool is_fd_in_valid_range(u32 fd)
{
   return fd < MAX_HANDLES;
}

static u32 get_free_handle_num(task_info *task)
{
   for (u32 free_fd = 0; free_fd < MAX_HANDLES; free_fd++)
      if (!task->pi->handles[free_fd])
         return free_fd;

   return (u32) -1;
}

/*
 * Even if getting the fs_handle this way is safe, using it won't be anymore
 * after thread-support is added to the kernel. For example, a thread might
 * work with given handle while another might destroy it.
 *
 * TODO: introduce a ref-count in the fs_base_handle struct and function like
 * put_fs_handle() or rename both to something like acquire/release_fs_handle.
 */
fs_handle get_fs_handle(u32 fd)
{
   task_info *curr = get_curr_task();
   fs_handle handle = NULL;

   disable_preemption();
   {
      if (is_fd_in_valid_range(fd) && curr->pi->handles[fd])
         handle = curr->pi->handles[fd];
   }
   enable_preemption();
   return handle;
}


sptr sys_open(const char *user_path, int flags, int mode)
{
   sptr ret;
   task_info *curr = get_curr_task();
   char *orig_path = curr->args_copybuf;
   char *path = curr->args_copybuf + ARGS_COPYBUF_SIZE / 2;
   size_t written = 0;
   fs_handle h = NULL;

   STATIC_ASSERT((ARGS_COPYBUF_SIZE / 2) >= MAX_PATH);

   ret = duplicate_user_path(orig_path, user_path, MAX_PATH, &written);

   if (ret != 0)
      return ret;

   disable_preemption();

   ret = compute_abs_path(orig_path, curr->pi->cwd, path, MAX_PATH);

   if (ret < 0) {
      ret = -ENAMETOOLONG;
      goto end;
   }

   u32 free_fd = get_free_handle_num(curr);

   if (!is_fd_in_valid_range(free_fd))
      goto no_fds;

   // TODO: make the vfs call runnable with preemption enabled
   // In order to achieve that, we'll need a per-process "fs" lock.
   ret = vfs_open(path, &h);

   if (ret < 0)
      goto end;

   ASSERT(h != NULL);

   curr->pi->handles[free_fd] = h;
   ret = (sptr) free_fd;

end:
   // printk("[TID: %i] sys_open('%s' => '%s', %x, %x) => %d\n",
   //        curr->tid, orig_path, path, flags, mode, ret);

   enable_preemption();
   return ret;

no_fds:
   ret = -EMFILE;
   goto end;
}

sptr sys_close(int user_fd)
{
   task_info *curr = get_curr_task();
   fs_handle handle;
   sptr ret = 0;
   u32 fd = (u32) user_fd;

   disable_preemption();

   handle = get_fs_handle(fd);

   if (!handle) {
      ret = -EBADF;
      goto end;
   }

   // TODO: in order to run with preemption enabled here, we'd need to have
   // a kind-of per-process (not per-task!) "fs" lock. Otherwise, if we make
   // this section preemptable, a close(handle) from other thread in the same
   // process could race with the one here below. At that point, the handle
   // object would be destroyed and we'll panic.

   vfs_close(handle);
   curr->pi->handles[fd] = NULL;

end:
   enable_preemption();
   return ret;
}

sptr sys_read(int user_fd, void *user_buf, size_t count)
{
   sptr ret;
   task_info *curr = get_curr_task();
   fs_handle handle;
   const u32 fd = (u32) user_fd;

   handle = get_fs_handle(fd);

   if (!handle)
      return -EBADF;

   count = MIN(count, IO_COPYBUF_SIZE);
   ret = vfs_read(curr->pi->handles[fd], curr->io_copybuf, count);

   if (ret > 0) {
      if (copy_to_user(user_buf, curr->io_copybuf, (size_t)ret) < 0) {
         // TODO: do we have to rewind the stream in this case?
         ret = -EFAULT;
         goto end;
      }
   }

end:
   return ret;
}

sptr sys_write(int user_fd, const void *user_buf, size_t count)
{
   task_info *curr = get_curr_task();
   fs_handle handle;
   sptr ret;
   const u32 fd = (u32) user_fd;

   handle = get_fs_handle(fd);

   if (!handle)
      return -EBADF;

   count = MIN(count, IO_COPYBUF_SIZE);
   ret = copy_from_user(curr->io_copybuf, user_buf, count);

   if (ret < 0)
      return -EFAULT;

   return vfs_write(handle, (char *)curr->io_copybuf, count);
}

sptr sys_ioctl(int user_fd, uptr request, void *argp)
{
   const u32 fd = (u32) user_fd;
   fs_handle handle = get_fs_handle(fd);

   if (!handle)
      return -EBADF;

   return vfs_ioctl(handle, request, argp);
}

sptr sys_writev(int user_fd, const struct iovec *user_iov, int user_iovcnt)
{
   task_info *curr = get_curr_task();
   const u32 fd = (u32) user_fd;
   const u32 iovcnt = (u32) user_iovcnt;
   fs_handle handle;
   sptr ret = 0;
   sptr rc;

   if (user_iovcnt <= 0)
      return -EINVAL;

   if (sizeof(struct iovec) * iovcnt > ARGS_COPYBUF_SIZE)
      return -EINVAL;

   rc = copy_from_user(curr->args_copybuf,
                       user_iov,
                       sizeof(struct iovec) * iovcnt);

   if (rc != 0)
      return -EFAULT;

   handle = get_fs_handle(fd);

   if (!handle)
      return -EBADF;

   vfs_exlock(handle);

   const struct iovec *iov = (const struct iovec *)curr->args_copybuf;

   // TODO: make the rest of the syscall run with preemption enabled.
   // In order to achieve that, it might be necessary to expose from vfs
   // a lock/unlock interface, or to entirely implement sys_writev in vfs.

   for (u32 i = 0; i < iovcnt; i++) {

      rc = sys_write(user_fd, iov[i].iov_base, iov[i].iov_len);

      if (rc < 0) {
         ret = rc;
         break;
      }

      ret += rc;

      if (rc < (sptr)iov[i].iov_len) {
         // For some reason (perfectly legit) we couldn't write the whole
         // user data (i.e. network card's buffers are full).
         break;
      }
   }

   vfs_exunlock(handle);
   return ret;
}

sptr sys_readv(int user_fd, const struct iovec *user_iov, int user_iovcnt)
{
   task_info *curr = get_curr_task();
   const u32 fd = (u32) user_fd;
   const u32 iovcnt = (u32) user_iovcnt;
   fs_handle handle;
   sptr ret = 0;
   sptr rc;

   if (user_iovcnt <= 0)
      return -EINVAL;

   if (sizeof(struct iovec) * iovcnt > ARGS_COPYBUF_SIZE)
      return -EINVAL;

   rc = copy_from_user(curr->args_copybuf,
                       user_iov,
                       sizeof(struct iovec) * iovcnt);

   if (rc != 0)
      return -EFAULT;

   handle = get_fs_handle(fd);

   if (!handle)
      return -EBADF;

   vfs_shlock(handle);

   const struct iovec *iov = (const struct iovec *)curr->args_copybuf;

   for (u32 i = 0; i < iovcnt; i++) {

      rc = sys_read(user_fd, iov[i].iov_base, iov[i].iov_len);

      if (rc < 0) {
         ret = rc;
         break;
      }

      ret += rc;

      if (rc < (sptr)iov[i].iov_len)
         break; // Not enough data to fill all the user buffers.
   }

   vfs_shunlock(handle);
   return ret;
}

sptr sys_stat64(const char *user_path, struct stat64 *user_statbuf)
{
   task_info *curr = get_curr_task();
   char *orig_path = curr->args_copybuf;
   char *path = curr->args_copybuf + ARGS_COPYBUF_SIZE / 2;
   struct stat64 statbuf;
   int rc = 0;
   fs_handle h = NULL;

   rc = copy_str_from_user(orig_path, user_path, MAX_PATH, NULL);

   if (rc < 0)
      return -EFAULT;

   if (rc > 0)
      return -ENAMETOOLONG;

   disable_preemption();
   {
      /*
       * No preemption because CWD may change.
       * TODO: introduce a per-process "big" lock.
       */
      rc = compute_abs_path(orig_path, curr->pi->cwd, path, MAX_PATH);
   }
   enable_preemption();

   if (rc < 0)
      return -ENAMETOOLONG;

   //printk("sys_stat64('%s') => vfs_open(%s)\n", orig_path, path);
   rc = vfs_open(path, &h);

   if (rc < 0)
      return rc;

   ASSERT(h != NULL);
   rc = vfs_stat64(h, &statbuf);

   if (rc < 0)
      goto out;

   rc = copy_to_user(user_statbuf, &statbuf, sizeof(struct stat64));

   if (rc < 0)
      return -EFAULT;

out:
   vfs_close(h);
   return rc;
}

sptr sys_lstat64(const char *user_path, struct stat64 *user_statbuf)
{
   /*
    * For moment, symlinks are not supported in Tilck. Therefore, make lstat()
    * behave exactly as stat().
    */

   return sys_stat64(user_path, user_statbuf);
}

sptr sys_readlink(const char *u_pathname, char *u_buf, size_t u_bufsize)
{
   /*
    * For moment, symlinks are not supported in Tilck. Therefore, just always
    * -EINVAL, the correct error for the case the named file is NOT a symbolic
    * link.
    */

   return -EINVAL;
}

sptr sys_llseek(u32 fd, size_t off_hi, size_t off_low, u64 *result, u32 whence)
{
   const s64 off64 = (s64)(((u64)off_hi << 32) | off_low);
   fs_handle handle;
   s64 new_off;
   int rc;

   STATIC_ASSERT(sizeof(new_off) >= sizeof(off_t));

   handle = get_fs_handle(fd);

   if (!handle)
      return -EBADF;

   new_off = vfs_seek(handle, off64, (int)whence);

   if (new_off < 0)
      return (sptr) new_off; /* return back vfs_seek's error */

   rc = copy_to_user(result, &new_off, sizeof(*result));

   if (rc != 0)
      return -EBADF;

   return 0;
}

sptr sys_getdents64(int user_fd, struct linux_dirent64 *user_dirp, u32 buf_size)
{
   const u32 fd = (u32) user_fd;
   fs_handle handle;
   int rc;

   //printk("[TID: %d] getdents64(fd: %d, dirp: %p, buf_size: %u)\n",
   //       get_curr_task()->tid, fd, user_dirp, buf_size);

   handle = get_fs_handle(fd);

   if (!handle)
      return -EBADF;

   rc = vfs_getdents64(handle, user_dirp, buf_size);
   return rc;
}

sptr sys_access(const char *pathname, int mode)
{
   // TODO: check mode and file r/w flags.
   return 0;
}

sptr sys_dup2(int oldfd, int newfd)
{
   sptr rc;
   fs_handle old_h, new_h;
   task_info *curr = get_curr_task();

   if (!is_fd_in_valid_range((u32) oldfd))
      return -EBADF;

   if (!is_fd_in_valid_range((u32) newfd))
      return -EBADF;

   if (newfd == oldfd)
      return -EINVAL;

   disable_preemption();

   old_h = get_fs_handle((u32) oldfd);

   if (!old_h) {
      rc = -EBADF;
      goto out;
   }

   new_h = get_fs_handle((u32) newfd);

   if (new_h) {
      vfs_close(new_h);
      new_h = NULL;
   }

   rc = vfs_dup(old_h, &new_h);

   if (rc != 0)
      goto out;

   curr->pi->handles[newfd] = new_h;
   rc = (sptr) newfd;

out:
   enable_preemption();
   return rc;
}

sptr sys_dup(int oldfd)
{
   sptr rc;
   u32 free_fd;

   disable_preemption();

   free_fd = get_free_handle_num(get_curr_task());

   if (!is_fd_in_valid_range(free_fd)) {
      rc = -EMFILE;
      goto out;
   }

   rc = sys_dup2(oldfd, (int) free_fd);

out:
   enable_preemption();
   return rc;
}

static void debug_print_fcntl_command(int cmd)
{
   switch (cmd) {

      case F_DUPFD:
         printk("fcntl: F_DUPFD\n");
         break;
      case F_DUPFD_CLOEXEC:
         printk("fcntl: F_DUPFD_CLOEXEC\n");
         break;
      case F_GETFD:
         printk("fcntl: F_GETFD\n");
         break;
      case F_SETFD:
         printk("fcntl: F_SETFD\n");
         break;
      case F_GETFL:
         printk("fcntl: F_GETFL\n");
         break;
      case F_SETFL:
         printk("fcntl: F_SETFL\n");
         break;
      case F_SETLK:
         printk("fcntl: F_SETLK\n");
         break;
      case F_SETLKW:
         printk("fcntl: F_SETLKW\n");
         break;
      case F_GETLK:
         printk("fcntl: F_GETLK\n");
         break;

      /* Skipping several other commands */

      default:
         printk("fcntl: unknown command\n");
   }
}

void close_cloexec_handles(process_info *pi)
{
   for (u32 i = 0; i < MAX_HANDLES; i++) {

      fs_handle_base *h = pi->handles[i];

      if (h && (h->flags & FD_CLOEXEC)) {
         vfs_close(h);
         pi->handles[i] = NULL;
      }
   }
}

sptr sys_fcntl64(int user_fd, int cmd, int arg)
{
   const u32 fd = (u32) user_fd;
   fs_handle_base *hb = get_fs_handle(fd);
   sptr rc = 0;

   if (!hb)
      return -EBADF;

   switch (cmd) {

      case F_SETFD:
         hb->flags = arg;
         break;

      case F_GETFD:
         return (sptr)hb->flags;

      default:
         rc = vfs_fcntl(hb, cmd, arg);
   }

   if (rc == -EINVAL) {
      printk("[fcntl64] Ignored unknown cmd %d\n", cmd);
   }

   return rc;
}
