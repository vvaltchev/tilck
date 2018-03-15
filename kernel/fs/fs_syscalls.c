
#include <common_defs.h>
#include <string_util.h>
#include <process.h>
#include <hal.h>
#include <fs/exvfs.h>
#include <exos_errno.h>

sptr sys_read(int fd, void *buf, size_t count)
{
   sptr ret;

   disable_preemption();

   if (fd < 0 || !current->handles[fd])
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

   if (fd < 0 || !current->handles[fd])
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
   printk("[kernel] sys_open(filename = '%s', "
          "flags = %x, mode = %x)\n", pathname, flags, mode);

   disable_preemption();

   u32 free_slot;
   for (free_slot = 0; free_slot < ARRAY_SIZE(current->handles); free_slot++) {
      if (!current->handles[free_slot])
         break;
   }

   if (free_slot == ARRAY_SIZE(current->handles)) {
      enable_preemption();
      return -EMFILE;
   }

   fs_handle h = exvfs_open(pathname);

   if (!h) {
      enable_preemption();
      return -ENOENT;
   }

   current->handles[free_slot] = h;
   enable_preemption();
   return free_slot;
}

sptr sys_close(int fd)
{
   printk("[kernel] sys_close(fd = %d)\n", fd);

   disable_preemption();

   if (fd < 0 || fd > (int)ARRAY_SIZE(current->handles) || !current->handles[fd]) {
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

   if (fd < 0)
      goto inval;

   if (fd > (int)ARRAY_SIZE(current->handles))
      goto badf;

   fs_handle h = current->handles[fd];

   if (!h)
      goto badf;

   ret = exvfs_ioctl(h, request, argp);

end:
   enable_preemption();
   return ret;

inval:
   ret = -EINVAL;
   goto end;

badf:
   ret = -EBADF;
   goto end;
}
