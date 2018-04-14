
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/process.h>
#include <exos/hal.h>
#include <exos/fs/exvfs.h>
#include <exos/errno.h>
#include <exos/user.h>

static char fs_user_buf[PAGE_SIZE];

static inline bool is_fd_valid(int fd)
{
   return fd >= 0 && fd < (int)ARRAY_SIZE(current->pi->handles);
}

sptr sys_read(int fd, void *buf, size_t count)
{
   sptr ret;

   disable_preemption();

   if (!is_fd_valid(fd) || !current->pi->handles[fd])
      goto badf;

   ret = exvfs_read(current->pi->handles[fd], buf, count);

end:
   enable_preemption();
   return ret;

badf:
   ret = -EBADF;
   goto end;
}

sptr sys_write(int fd, const void *buf, size_t count)
{
   sptr ret;
   count = MIN(count, sizeof(fs_user_buf));

   disable_preemption();

   ret = copy_from_user(fs_user_buf, buf, count);

   if (ret < 0) {
      ret = -EFAULT;
      goto end;
   }

   if (!is_fd_valid(fd) || !current->pi->handles[fd])
      goto badf;

   ret = exvfs_write(current->pi->handles[fd], (char *)fs_user_buf, count);

end:
   enable_preemption();
   return ret;

badf:
   ret = -EBADF;
   goto end;
}

int get_free_handle_num(task_info *task)
{
   for (u32 free_fd = 0; free_fd < ARRAY_SIZE(task->pi->handles); free_fd++)
      if (!task->pi->handles[free_fd])
         return free_fd;

   return -1;
}

sptr sys_open(const char *pathname, int flags, int mode)
{
   sptr ret;

   printk("sys_open(filename = '%s', "
          "flags = %x, mode = %x)\n", pathname, flags, mode);

   disable_preemption();

   int free_fd = get_free_handle_num(current);

   if (!is_fd_valid(free_fd))
      goto no_fds;

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
   printk("sys_close(fd = %d)\n", fd);

   disable_preemption();

   if (!is_fd_valid(fd) || !current->pi->handles[fd]) {
      enable_preemption();
      return -EBADF;
   }

   exvfs_close(current->pi->handles[fd]);
   current->pi->handles[fd] = NULL;

   enable_preemption();
   return 0;
}

sptr sys_ioctl(int fd, uptr request, void *argp)
{
   sptr ret = -EINVAL;
   disable_preemption();

   if (!is_fd_valid(fd) || !current->pi->handles[fd])
      goto badf;

   ret = exvfs_ioctl(current->pi->handles[fd], request, argp);

end:
   enable_preemption();
   return ret;

badf:
   ret = -EBADF;
   goto end;
}

typedef struct {
   void *iov_base;    /* Starting address */
   size_t iov_len;    /* Number of bytes to transfer */
} iovec;

sptr sys_writev(int fd, const iovec *iov, int iovcnt)
{
   sptr written = 0;
   disable_preemption();

   for (int i = 0; i < iovcnt; i++) {

      sptr rc = sys_write(fd, iov[i].iov_base, iov[i].iov_len);

      if (rc < 0) {
         written = rc;
         goto out;
      }

      written += rc;
   }

out:
   enable_preemption();
   return written;
}
