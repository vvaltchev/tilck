
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

static char rw_copybuf[PAGE_SIZE];
static iovec iovec_copybuf[32];

static inline bool is_fd_valid(int fd)
{
   return fd >= 0 && fd < (int)ARRAY_SIZE(current->pi->handles);
}

int get_free_handle_num(task_info *task)
{
   for (u32 free_fd = 0; free_fd < ARRAY_SIZE(task->pi->handles); free_fd++)
      if (!task->pi->handles[free_fd])
         return free_fd;

   return -1;
}

sptr sys_read(int fd, void *buf, size_t count)
{
   sptr ret;

   disable_preemption();

   if (!is_fd_valid(fd) || !current->pi->handles[fd]) {
      ret = -EBADF;
      goto end;
   }

   // TODO: make the exvfs call runnable with preemption enabled
   ret = exvfs_read(current->pi->handles[fd], buf, count);

end:
   enable_preemption();
   return ret;
}

sptr sys_write(int fd, const void *buf, size_t count)
{
   sptr ret;
   count = MIN(count, sizeof(rw_copybuf));

   disable_preemption();

   ret = copy_from_user(rw_copybuf, buf, count);

   if (ret < 0) {
      ret = -EFAULT;
      goto end;
   }

   if (!is_fd_valid(fd) || !current->pi->handles[fd]) {
      ret = -EBADF;
      goto end;
   }

   // TODO: make the exvfs call runnable with preemption enabled
   ret = exvfs_write(current->pi->handles[fd], (char *)rw_copybuf, count);

end:
   enable_preemption();
   return ret;
}

sptr sys_open(const char *pathname, int flags, int mode)
{
   sptr ret;
   disable_preemption();

   printk("sys_open(filename = '%s', "
          "flags = %x, mode = %x)\n", pathname, flags, mode);

   int free_fd = get_free_handle_num(current);

   if (!is_fd_valid(free_fd))
      goto no_fds;

   // TODO: make the exvfs call runnable with preemption enabled
   fs_handle h = exvfs_open(pathname);

   if (!h)
      goto no_ent;

   current->pi->handles[free_fd] = h;
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
   printk("sys_close(fd = %d)\n", fd);

   disable_preemption();

   if (!is_fd_valid(fd) || !current->pi->handles[fd]) {
      ret = -EBADF;
      goto end;
   }

   // TODO: make the exvfs call runnable with preemption enabled
   exvfs_close(current->pi->handles[fd]);
   current->pi->handles[fd] = NULL;

end:
   enable_preemption();
   return ret;
}

sptr sys_ioctl(int fd, uptr request, void *argp)
{
   sptr ret = -EINVAL;
   disable_preemption();

   if (!is_fd_valid(fd) || !current->pi->handles[fd]) {
      ret = -EBADF;
      goto end;
   }

   // TODO: make the exvfs call runnable with preemption enabled
   ret = exvfs_ioctl(current->pi->handles[fd], request, argp);

end:
   enable_preemption();
   return ret;
}

sptr sys_writev(int fd, const iovec *iov, int iovcnt)
{
   sptr rc;
   sptr ret = 0;
   disable_preemption();

   if (iovcnt > (int)ARRAY_SIZE(iovec_copybuf))
      return -EINVAL;

   rc = copy_from_user(iovec_copybuf, iov, sizeof(iovec) * iovcnt);

   if (rc != 0) {
      ret = -EFAULT;
      goto out;
   }

   iov = (const iovec *)&iovec_copybuf;

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
