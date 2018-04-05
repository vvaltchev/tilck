
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/process.h>
#include <exos/hal.h>
#include <exos/fs/exvfs.h>
#include <exos/errno.h>

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

   disable_preemption();

   if (!is_fd_valid(fd) || !current->pi->handles[fd])
      goto badf;

   ret = exvfs_write(current->pi->handles[fd], (char *)buf, count);

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

   printk("[kernel] sys_open(filename = '%s', "
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
   printk("[kernel] sys_close(fd = %d)\n", fd);

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

   //printk("[kernel] ioctl(fd: %i, request: %p, argp: %p)\n", fd, request, argp);

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
