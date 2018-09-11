
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/fs/devfs.h>
#include <tilck/kernel/errno.h>

static ssize_t fb_read(fs_handle fsh, char *buf, size_t size)
{
   return 0;
}

static ssize_t fb_write(fs_handle h, char *buf, size_t size)
{
   return -ENOSPC;
}

static int fb_ioctl(fs_handle h, uptr request, void *argp)
{
   return -EINVAL;
}

static int create_fb_device(int minor, file_ops *ops, devfs_entry_type *t)
{
   *t = DEVFS_CHAR_DEVICE;
   bzero(ops, sizeof(file_ops));

   ops->read = fb_read;
   ops->write = fb_write;
   ops->ioctl = fb_ioctl;
   ops->seek = NULL;

   return 0;
}

void init_fbdev(void)
{
   driver_info *di = kmalloc(sizeof(driver_info));

   if (!di)
      panic("TTY: no enough memory for init_tty()");

   di->name = "fb";
   di->create_dev_file = create_fb_device;
   int major = register_driver(di);
   int rc = create_dev_file("fb0", major, 0 /* minor */);

   if (rc != 0)
      panic("TTY: unable to create /dev/fb0 (error: %d)", rc);
}
