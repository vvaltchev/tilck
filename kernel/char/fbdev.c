/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/fs/devfs.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/fb_console.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/tty.h>
#include <tilck/kernel/sched.h>

#include <linux/fb.h>     // system header
#include <linux/major.h>  // system header

static ssize_t total_fb_pages_mapped;

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

   total_fb_pages_mapped += len >> PAGE_SHIFT;
   return 0;
}

static int fbdev_munmap(fs_handle h /* ignored */, void *vaddr, size_t len)
{
   ASSERT(IS_PAGE_ALIGNED(len));
   unmap_pages(get_curr_pdir(), vaddr, len >> PAGE_SHIFT, false);

   total_fb_pages_mapped -= len >> PAGE_SHIFT;
   ASSERT(total_fb_pages_mapped >= 0);

   /*
    * [BE_NICE] In case we're in a dying task() [we've been called indirectly by
    * terminate_process()] and no other process in the system has any pages of
    * the framebuffer mapped, then make sure that the current TTY is restored
    * back in KD_TEXT mode, in order to give back to the user the control.
    *
    * NOTE: that's a special Tilck-only behavior: on Linux, TTY won't be
    * restored in KD_TEXT mode and, therefore, the system won't be usable by
    * a user physically near the machine. Not even switching to a Xorg instance
    * with ALT+F1, ALT+F2, etc. works. It's required to use Magic SysRq
    * shortcuts to reboot the machine or connect to it remotely to do that.
    */
   if (total_fb_pages_mapped == 0 && in_currently_dying_task()) {
      tty_restore_kd_text_mode(get_curr_tty());
   }

   return 0;
}

static int
create_fb_device(int minor, const file_ops **fops_ref, enum devfs_entry *t)
{
   static const file_ops static_ops_fb = {
      .read = fb_read,
      .write = fb_write,
      .ioctl = fb_ioctl,
      .mmap = fbdev_mmap,
      .munmap = fbdev_munmap,
   };

   *t = DEVFS_CHAR_DEVICE;
   *fops_ref = &static_ops_fb;
   return 0;
}

void init_fbdev(void)
{
   if (!use_framebuffer())
      return;

   driver_info *di = kmalloc(sizeof(driver_info));

   if (!di)
      panic("TTY: no enough memory for init_tty()");

   di->name = "fb";
   di->create_dev_file = create_fb_device;
   register_driver(di, FB_MAJOR);
   int rc = create_dev_file("fb0", FB_MAJOR, 0 /* minor */);

   if (rc != 0)
      panic("TTY: unable to create /dev/fb0 (error: %d)", rc);
}
