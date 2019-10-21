/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/fs/devfs.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/fb_console.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/paging_hw.h>
#include <tilck/kernel/tty.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/process_mm.h>

#include <linux/fb.h>     // system header
#include <linux/major.h>  // system header
#include <sys/mman.h>     // system header

static ssize_t total_fb_pages_mapped;
static struct list mappings_list = make_list(mappings_list);

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

static int
fbdev_mmap(struct user_mapping *um, bool register_only)
{
   ASSERT(IS_PAGE_ALIGNED(um->len));

   if (um->off != 0)
      return -EINVAL; /* not supported, at least for the moment */

   if (register_only)
      goto register_mapping;

   fb_user_mmap(um->vaddrp, um->len);

   total_fb_pages_mapped += um->len >> PAGE_SHIFT;

register_mapping:
   list_add_tail(&mappings_list, &um->inode_node);
   return 0;
}

static int fbdev_munmap(fs_handle h /* ignored */, void *vaddr, size_t len)
{
   size_t unmapped_count;
   ASSERT(IS_PAGE_ALIGNED(len));

   unmapped_count = unmap_pages_permissive(
      get_curr_pdir(),
      vaddr,
      len >> PAGE_SHIFT,
      false
   );

   total_fb_pages_mapped -= (size_t)unmapped_count;
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
create_fb_device(int minor,
                 const struct file_ops **fops_ref,
                 enum vfs_entry_type *t)
{
   static const struct file_ops static_ops_fb = {
      .read = fb_read,
      .write = fb_write,
      .ioctl = fb_ioctl,
      .mmap = fbdev_mmap,
      .munmap = fbdev_munmap,
   };

   *t = VFS_CHAR_DEV;
   *fops_ref = &static_ops_fb;
   return 0;
}

void init_fbdev(void)
{
   if (!use_framebuffer())
      return;

   struct driver_info *di = kmalloc(sizeof(struct driver_info));

   if (!di)
      panic("TTY: no enough memory for init_tty()");

   di->name = "fb";
   di->create_dev_file = create_fb_device;
   register_driver(di, FB_MAJOR);
   int rc = create_dev_file("fb0", FB_MAJOR, 0 /* minor */);

   if (rc != 0)
      panic("TTY: unable to create /dev/fb0 (error: %d)", rc);
}
