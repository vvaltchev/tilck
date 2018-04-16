
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/process.h>
#include <exos/hal.h>
#include <exos/fs/exvfs.h>
#include <exos/errno.h>
#include <exos/user.h>

typedef struct {
   void *iov_base;    /* Starting address */
   size_t iov_len;    /* Number of bytes to transfer */
} iovec;

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

sptr sys_read(int fd, void *user_buf, size_t count)
{
   sptr ret;
   task_info *curr = get_curr_task();

   disable_preemption();

   if (!is_fd_valid(fd) || !curr->pi->handles[fd]) {
      ret = -EBADF;
      goto end;
   }

   count = MIN(count, IO_COPYBUF_SIZE);

   // TODO: make the exvfs call runnable with preemption enabled
   ret = exvfs_read(curr->pi->handles[fd], curr->io_copybuf, count);

   if (ret > 0) {
      if (copy_to_user(user_buf, curr->io_copybuf, ret) < 0) {
         // TODO: do we have to rewind the stream in this case?
         ret = -EFAULT;
         goto end;
      }
   }

end:
   enable_preemption();
   return ret;
}

sptr sys_write(int fd, const void *user_buf, size_t count)
{
   sptr ret;
   task_info *curr = get_curr_task();

   count = MIN(count, IO_COPYBUF_SIZE);

   disable_preemption();

   ret = copy_from_user(curr->io_copybuf, user_buf, count);

   if (ret < 0) {
      ret = -EFAULT;
      goto end;
   }

   if (!is_fd_valid(fd) || !curr->pi->handles[fd]) {
      ret = -EBADF;
      goto end;
   }

   // TODO: make the exvfs call runnable with preemption enabled

   ret = exvfs_write(curr->pi->handles[fd],
                     (char *)curr->io_copybuf,
                     count);

end:
   enable_preemption();
   return ret;
}

sptr sys_open(const char *pathname, int flags, int mode)
{
   sptr ret;
   task_info *curr = get_curr_task();

   disable_preemption();

   ret = copy_str_from_user(curr->args_copybuf, pathname, ARGS_COPYBUF_SIZE);

   if (ret < 0) {
      ret = -EFAULT;
      goto end;
   }

   if (ret > 0) {
      ret = -ENAMETOOLONG;
      goto end;
   }

   printk("sys_open(filename = '%s', "
          "flags = %x, mode = %x)\n", pathname, flags, mode);

   int free_fd = get_free_handle_num(curr);

   if (!is_fd_valid(free_fd))
      goto no_fds;

   // TODO: make the exvfs call runnable with preemption enabled
   fs_handle h = exvfs_open(pathname);

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
   sptr ret = 0;
   task_info *curr = get_curr_task();

   printk("sys_close(fd = %d)\n", fd);

   disable_preemption();

   if (!is_fd_valid(fd) || !curr->pi->handles[fd]) {
      ret = -EBADF;
      goto end;
   }

   // TODO: make the exvfs call runnable with preemption enabled
   exvfs_close(curr->pi->handles[fd]);
   curr->pi->handles[fd] = NULL;

end:
   enable_preemption();
   return ret;
}

sptr sys_ioctl(int fd, uptr request, void *argp)
{
   sptr ret = -EINVAL;
   task_info *curr = get_curr_task();

   disable_preemption();

   if (!is_fd_valid(fd) || !curr->pi->handles[fd]) {
      ret = -EBADF;
      goto end;
   }

   // TODO: make the exvfs call runnable with preemption enabled
   ret = exvfs_ioctl(curr->pi->handles[fd], request, argp);

end:
   enable_preemption();
   return ret;
}

sptr sys_writev(int fd, const iovec *user_iov, int iovcnt)
{
   sptr rc;
   sptr ret = 0;
   task_info *curr = get_curr_task();

   disable_preemption();

   if (sizeof(iovec) * iovcnt > ARGS_COPYBUF_SIZE)
      return -EINVAL;

   rc = copy_from_user(curr->args_copybuf, user_iov, sizeof(iovec) * iovcnt);

   if (rc != 0) {
      ret = -EFAULT;
      goto out;
   }

   const iovec *iov = (const iovec *)curr->args_copybuf;

   // TODO: make the rest of the syscall run with preemption enabled.
   // In order to achieve that, it might be necessary to expose from exvfs
   // a lock/unlock interface, or to entirely implement sys_writev in exvfs.

   for (int i = 0; i < iovcnt; i++) {

      rc = sys_write(fd, iov[i].iov_base, iov[i].iov_len);

      if (rc < 0) {
         ret = rc;
         goto out;
      }

      ret += rc;
   }

out:
   enable_preemption();
   return ret;
}
