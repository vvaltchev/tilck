
#include <common_defs.h>
#include <string_util.h>
#include <process.h>
#include <hal.h>
#include <fs/exvfs.h>
#include <exos_errno.h>


sptr sys_open(const char *pathname, int flags, int mode)
{
   printk("[kernel] sys_open(filename = '%s', "
          "flags = %x, mode = %x)\n", pathname, flags, mode);

   disable_preemption();

   fs_handle *h = exvfs_open(pathname);

   if (!h) {
      enable_preemption();
      return -ENOENT;
   }

   u32 free_slot;
   for (free_slot = 0; free_slot < ARRAY_SIZE(current->handles); free_slot++) {
      if (!current->handles[free_slot])
         break;
   }

   current->handles[free_slot] = h;
   enable_preemption();

   return free_slot;
}

sptr sys_close(int fd)
{
   printk("[kernel] sys_close(fd = %d)\n", fd);

   disable_preemption();

   if (fd < 0 || !current->handles[fd]) {
      enable_preemption();
      return -EBADF;
   }

   //printk("pid: %i, close handle: %i, hnd: %p\n", current->pid, fd, current->handles[fd]);
   exvfs_close(current->handles[fd]);
   current->handles[fd] = NULL;

   enable_preemption();
   return 0;
}


sptr sys_read(int fd, void *buf, size_t count)
{
   //printk("sys_read(fd = %i, count = %u)\n", fd, count);
   return 0;
}
