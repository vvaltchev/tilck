
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

   if (fd < 0)
      goto badf;

   fs_handle *h = current->handles[fd];

   if (!h)
      goto badf;

   ret = exvfs_read(h, buf, count);

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

   if (fd < 0)
      goto badf;

   fs_handle *h = current->handles[fd];

   if (!h)
      goto badf;

   ret = exvfs_write(h, (char *)buf, count);

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


/* ------------ IOCTL hacks ---------------- */

#define TCGETS 0x00005401

typedef unsigned char   cc_t;
typedef unsigned int    speed_t;
typedef unsigned int    tcflag_t;


#define NCCS 19
typedef struct {
   tcflag_t c_iflag;           /* input mode flags */
   tcflag_t c_oflag;           /* output mode flags */
   tcflag_t c_cflag;           /* control mode flags */
   tcflag_t c_lflag;           /* local mode flags */
   cc_t c_line;                /* line discipline */
   cc_t c_cc[NCCS];            /* control characters */
} termios;

static const termios hard_coded_termios =
{
   0x4500,
   0x05,
   0xbf,
   0x8a3b,
   0,
   {
      0x3, 0x1c, 0x7f, 0x15, 0x4, 0x0, 0x1, 0x0,
      0x11, 0x13, 0x1a, 0x0, 0x12, 0xf, 0x17, 0x16,
      0x0, 0x0, 0x0
   },
};

extern filesystem *devfs;

sptr sys_ioctl(int fd, uptr request, void *argp)
{
   printk("[kernel] ioctl(fd: %i, request: %p, argp: %p)\n", fd, request, argp);

   disable_preemption();

   if (fd < 0 || fd > (int)ARRAY_SIZE(current->handles)) {
      enable_preemption();
      return -EINVAL;
   }

   fs_handle *h = current->handles[fd];

   if (!h) {
      enable_preemption();
      return -EBADF;
   }

   // This is a DIRTY HACK
   // TODO: forward this call to ioctl() to the right device in devfs
   // by adding a proper ioctl func in fileops.

   if (request == TCGETS) {
      memmove(argp, &hard_coded_termios, sizeof(termios));
      enable_preemption();
      return 0;
   }

   enable_preemption();
   return -EINVAL;
}
