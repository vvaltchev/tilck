
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/fs/devfs.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/fb_console.h>
#include <tilck/kernel/paging.h>

#include <linux/fb.h> // system header

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
   if (request == FBIOGET_FSCREENINFO) {

      struct fb_fix_screeninfo fix_info;
      int rc;

      fb_fill_fix_info(&fix_info);
      rc = copy_to_user(argp, &fix_info, sizeof(fix_info));

      if (rc != 0)
         return -EFAULT;

      return 0;
   }

   if (request == FBIOGET_VSCREENINFO) {

      struct fb_var_screeninfo var_info;
      int rc;

      fb_fill_var_info(&var_info);
      rc = copy_to_user(argp, &var_info, sizeof(var_info));

      if (rc != 0)
         return -EFAULT;

      return 0;
   }

   return -EINVAL;
}

static int fbdev_mmap(fs_handle h /* ignored */, void *vaddr, size_t len)
{
   ASSERT(IS_PAGE_ALIGNED(len));
   fb_user_mmap(vaddr, len);
   return 0;
}

static int fbdev_munmap(fs_handle h /* ignored */, void *vaddr, size_t len)
{
   ASSERT(IS_PAGE_ALIGNED(len));
   unmap_pages(get_curr_pdir(), vaddr, len >> PAGE_SHIFT, false);
   return 0;
}

static int create_fb_device(int minor, file_ops *ops, devfs_entry_type *t)
{
   *t = DEVFS_CHAR_DEVICE;
   bzero(ops, sizeof(file_ops));

   ops->read = fb_read;
   ops->write = fb_write;
   ops->ioctl = fb_ioctl;
   ops->mmap = fbdev_mmap;
   ops->munmap = fbdev_munmap;

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
