
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/process.h>
#include <exos/hal.h>
#include <exos/fs/exvfs.h>
#include <exos/errno.h>
#include <exos/user.h>

static inline bool is_fd_valid(int fd)
{
   return fd >= 0 && fd < (int)ARRAY_SIZE(get_curr_task()->pi->handles);
}

int get_free_handle_num(task_info *task)
{
   for (u32 free_fd = 0; free_fd < ARRAY_SIZE(task->pi->handles); free_fd++)
      if (!task->pi->handles[free_fd])
         return free_fd;

   return -1;
}

static fs_handle get_fs_handle(int fd)
{
   task_info *curr = get_curr_task();
   fs_handle handle = NULL;

   disable_preemption();
   {
      if (is_fd_valid(fd) && curr->pi->handles[fd])
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

   STATIC_ASSERT((ARGS_COPYBUF_SIZE / 2) >= MAX_PATH);

   ret = duplicate_user_path(orig_path, user_path, MAX_PATH, &written);

   if (ret != 0)
      return ret;

   printk("[TID: %i] sys_open('%s', %x, %x)\n", curr->tid, orig_path, flags, mode);

   ret = compute_abs_path(orig_path, curr->pi->cwd, path, MAX_PATH);

   if (ret < 0)
      return -ENAMETOOLONG;

   disable_preemption();

   int free_fd = get_free_handle_num(curr);

   if (!is_fd_valid(free_fd))
      goto no_fds;

   // TODO: make the exvfs call runnable with preemption enabled
   // In order to achieve that, we'll need a per-process "fs" lock.
   fs_handle h = exvfs_open(path);

   if (!h)
      goto no_ent;

   curr->pi->handles[free_fd] = h;
   ret = free_fd;

end:
   enable_preemption();
   return ret;

no_fds:
   ret = -EMFILE;
   goto end;

no_ent:
   ret = -ENOENT;
   goto end;
}

sptr sys_close(int fd)
{
   task_info *curr = get_curr_task();
   fs_handle handle;
   sptr ret = 0;

   printk("[TID: %i] sys_close(fd = %d)\n", curr->tid, fd);

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

   exvfs_exlock(handle);
   {
      exvfs_close(handle);
      curr->pi->handles[fd] = NULL;
   }
   exvfs_exunlock(handle);

   enable_preemption();
end:
   return ret;
}

sptr sys_read(int fd, void *user_buf, size_t count)
{
   sptr ret;
   task_info *curr = get_curr_task();
   fs_handle handle;

   handle = get_fs_handle(fd);

   if (!handle)
      return -EBADF;

   count = MIN(count, IO_COPYBUF_SIZE);

   exvfs_exlock(handle);
   {
      ret = exvfs_read(curr->pi->handles[fd], curr->io_copybuf, count);
   }
   exvfs_exunlock(handle);

   if (ret > 0) {
      if (copy_to_user(user_buf, curr->io_copybuf, ret) < 0) {
         // TODO: do we have to rewind the stream in this case?
         ret = -EFAULT;
         goto end;
      }
   }

end:
   return ret;
}

sptr sys_write(int fd, const void *user_buf, size_t count)
{
   task_info *curr = get_curr_task();
   fs_handle handle;
   sptr ret;

   count = MIN(count, IO_COPYBUF_SIZE);
   ret = copy_from_user(curr->io_copybuf, user_buf, count);

   if (ret < 0)
      return -EFAULT;

   handle = get_fs_handle(fd);

   if (!handle)
      return -EBADF;

   exvfs_exlock(handle);
   {
      ret = exvfs_write(handle, (char *)curr->io_copybuf, count);
   }
   exvfs_exunlock(handle);

   return ret;
}

sptr sys_ioctl(int fd, uptr request, void *argp)
{
   sptr ret;
   fs_handle handle;

   handle = get_fs_handle(fd);

   if (!handle)
      return -EBADF;

   exvfs_exlock(handle);
   {
      ret = exvfs_ioctl(handle, request, argp);
   }
   exvfs_exunlock(handle);
   return ret;
}

sptr sys_writev(int fd, const struct iovec *user_iov, int iovcnt)
{
   task_info *curr = get_curr_task();
   fs_handle handle;
   sptr ret = 0;
   sptr rc;

   if (sizeof(struct iovec) * iovcnt > ARGS_COPYBUF_SIZE)
      return -EINVAL;

   rc = copy_from_user(curr->args_copybuf, user_iov, sizeof(struct iovec) * iovcnt);

   if (rc != 0)
      return -EFAULT;

   handle = get_fs_handle(fd);

   if (!handle)
      return -EBADF;

   exvfs_exlock(handle);

   const struct iovec *iov = (const struct iovec *)curr->args_copybuf;

   // TODO: make the rest of the syscall run with preemption enabled.
   // In order to achieve that, it might be necessary to expose from exvfs
   // a lock/unlock interface, or to entirely implement sys_writev in exvfs.

   for (int i = 0; i < iovcnt; i++) {

      rc = sys_write(fd, iov[i].iov_base, iov[i].iov_len);

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

   exvfs_exunlock(handle);
   return ret;
}

sptr sys_readv(int fd, const struct iovec *user_iov, int iovcnt)
{
   task_info *curr = get_curr_task();
   fs_handle handle;
   sptr ret = 0;
   sptr rc;

   if (sizeof(struct iovec) * iovcnt > ARGS_COPYBUF_SIZE)
      return -EINVAL;

   rc = copy_from_user(curr->args_copybuf, user_iov, sizeof(struct iovec) * iovcnt);

   if (rc != 0)
      return -EFAULT;

   handle = get_fs_handle(fd);

   if (!handle)
      return -EBADF;

   exvfs_exlock(handle);

   const struct iovec *iov = (const struct iovec *)curr->args_copybuf;

   for (int i = 0; i < iovcnt; i++) {

      rc = sys_read(fd, iov[i].iov_base, iov[i].iov_len);

      if (rc < 0) {
         ret = rc;
         break;
      }

      ret += rc;

      if (rc < (sptr)iov[i].iov_len)
         break; // Not enough data to fill all the user buffers.
   }

   exvfs_exunlock(handle);
   return ret;
}

sptr sys_stat64(const char *user_path, struct stat *user_statbuf)
{
   task_info *curr = get_curr_task();
   char *orig_path = curr->args_copybuf;
   char *path = curr->args_copybuf + ARGS_COPYBUF_SIZE / 2;
   struct stat statbuf;
   int rc = 0;

   rc = copy_str_from_user(orig_path, user_path, MAX_PATH, NULL);

   if (rc < 0)
      return -EFAULT;

   if (rc > 0)
      return -ENAMETOOLONG;

   rc = compute_abs_path(orig_path, curr->pi->cwd, path, MAX_PATH);
   VERIFY(rc == 0); /* orig_path is at most MAX_PATH and cannot get longer */

   printk("sys_stat64('%s')\n", path);

   fs_handle h = exvfs_open(path);

   if (!h)
      return -ENOENT;

   rc = exvfs_stat(h, &statbuf);

   if (rc < 0)
      goto out;

   rc = copy_to_user(user_statbuf, &statbuf, sizeof(struct stat));

   if (rc < 0)
      return -EFAULT;

out:
   exvfs_close(h);
   return rc;
}

sptr sys_lstat64(const char *user_path, struct stat *user_statbuf)
{
   /*
    * For moment, symlinks are not supported in exOS. Therefore, make lstat()
    * behave exactly as stat().
    */
   return sys_stat64(user_path, user_statbuf);
}
