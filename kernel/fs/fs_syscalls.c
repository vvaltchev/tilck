
#include <common_defs.h>
#include <string_util.h>
#include <process.h>
#include <hal.h>
#include <fs/exvfs.h>
#include <exos_errno.h>

static inline bool is_fd_valid(int fd)
{
   return fd >= 0 && fd < (int)ARRAY_SIZE(current->handles);
}

sptr sys_read(int fd, void *buf, size_t count)
{
   sptr ret;

   disable_preemption();

   if (!is_fd_valid(fd) || !current->handles[fd])
      goto badf;

   ret = exvfs_read(current->handles[fd], buf, count);

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

   if (!is_fd_valid(fd) || !current->handles[fd])
      goto badf;

   ret = exvfs_write(current->handles[fd], (char *)buf, count);

end:
   enable_preemption();
   return ret;

badf:
   ret = -EBADF;
   goto end;
}

sptr sys_open(const char *pathname, int flags, int mode)
{
   sptr ret;

   printk("[kernel] sys_open(filename = '%s', "
          "flags = %x, mode = %x)\n", pathname, flags, mode);

   disable_preemption();

   u32 free_slot;
   for (free_slot = 0; free_slot < ARRAY_SIZE(current->handles); free_slot++) {
      if (!current->handles[free_slot])
         break;
   }

   if (free_slot == ARRAY_SIZE(current->handles))
      goto no_fds;

   fs_handle h = exvfs_open(pathname);

   if (!h)
      goto no_ent;

   current->handles[free_slot] = h;
   ret = free_slot;

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

   if (!is_fd_valid(fd) || !current->handles[fd]) {
      enable_preemption();
      return -EBADF;
   }

   exvfs_close(current->handles[fd]);
   current->handles[fd] = NULL;

   enable_preemption();
   return 0;
}

sptr sys_ioctl(int fd, uptr request, void *argp)
{
   sptr ret = -EINVAL;

   printk("[kernel] ioctl(fd: %i, request: %p, argp: %p)\n", fd, request, argp);

   disable_preemption();

   if (!is_fd_valid(fd) || !current->handles[fd])
      goto badf;

   ret = exvfs_ioctl(current->handles[fd], request, argp);

end:
   enable_preemption();
   return ret;

badf:
   ret = -EBADF;
   goto end;
}
