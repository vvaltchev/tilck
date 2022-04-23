/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

#include <tilck/kernel/modules.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/user.h>
#include <tilck/mods/fb_console.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/paging_hw.h>
#include <tilck/kernel/tty.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/process_mm.h>
#include <tilck/kernel/fs/devfs.h>
#include <tilck/kernel/fs/vfs.h>

#include <linux/fb.h>     // system header
#include <linux/major.h>  // system header
#include <sys/mman.h>     // system header

#include "fb_int.h"

extern ulong fb_vaddr;
extern u32 fb_size;

static ssize_t total_fb_pages_mapped;
static struct list mappings_list = STATIC_LIST_INIT(mappings_list);

static ssize_t fb_read(fs_handle h, char *user_buf, size_t size, offt *pos)
{
   ssize_t actual_size = MIN((offt)fb_size - *pos, (offt)size);
   void *src = (char *)fb_vaddr + *pos;

   *pos += actual_size;

   if (copy_to_user(user_buf, src, (size_t)actual_size))
      return -EFAULT;

   return actual_size;
}

static ssize_t fb_write(fs_handle h, char *user_buf, size_t size, offt *pos)
{
   ssize_t actual_size = MIN((offt)fb_size - *pos, (offt)size);
   void *dest = (char *)fb_vaddr + *pos;
   *pos += (offt)actual_size;

   if (copy_from_user(dest, user_buf, (size_t)actual_size))
      return -EFAULT;

   return actual_size;
}

static offt fb_seek(fs_handle h, offt off, int whence)
{
   struct devfs_handle *dh = h;
   offt new_pos = -1;

   switch (whence) {

      case SEEK_SET:
         new_pos = off;
         break;

      case SEEK_CUR:
         new_pos = dh->h_fpos + off;
         break;

      case SEEK_END:
         new_pos = dh->h_fpos - off;
         break;
   }

   if (new_pos < 0 || (size_t)new_pos > fb_size)
      return -EINVAL;

   dh->h_fpos = new_pos;
   return dh->h_fpos;
}

static int fb_ioctl(fs_handle h, ulong request, void *argp)
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
fbdev_mmap(struct user_mapping *um, pdir_t *pdir, int flags)
{
   ASSERT(IS_PAGE_ALIGNED(um->len));

   if (um->off != 0)
      return -EINVAL; /* not supported, at least for the moment */

   if (flags & VFS_MM_DONT_MMAP)
      goto register_mapping;

   fb_user_mmap(pdir, um->vaddrp, um->len);
   total_fb_pages_mapped += um->len >> PAGE_SHIFT;

register_mapping:
   if (!(flags & VFS_MM_DONT_REGISTER)) {
      list_add_tail(&mappings_list, &um->inode_node);
   }

   return 0;
}

static int fbdev_munmap(struct user_mapping *um, void *vaddr, size_t len)
{
   struct fs_handle_base *hb = um->h;
   size_t unmapped_count;
   bool dying_task = false;

   ASSERT(IS_PAGE_ALIGNED(len));
   ASSERT(hb->pi == get_curr_proc());

   if (get_process_task(hb->pi)->state == TASK_STATE_ZOMBIE)
      dying_task = true;

   unmapped_count = unmap_pages_permissive(
      hb->pi->pdir,
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
   if (total_fb_pages_mapped == 0 && dying_task) {
      tty_restore_kd_text_mode(hb->pi->proc_tty);
   }

   return 0;
}

static int
create_fb_device(int minor,
                 enum vfs_entry_type *type,
                 struct devfs_file_info *nfo)
{
   static const struct file_ops static_ops_fb = {
      .read = fb_read,
      .write = fb_write,
      .seek = fb_seek,
      .ioctl = fb_ioctl,
      .mmap = fbdev_mmap,
      .munmap = fbdev_munmap,
   };

   *type = VFS_CHAR_DEV;
   nfo->fops = &static_ops_fb;
   nfo->spec_flags = VFS_SPFL_NO_USER_COPY | VFS_SPFL_MMAP_SUPPORTED;
   return 0;
}

static void init_fbdev(void)
{
   if (!use_framebuffer())
      return;

   struct driver_info *di = kalloc_obj(struct driver_info);

   if (!di)
      panic("TTY: no enough memory for init_tty()");

   di->name = "fb";
   di->create_dev_file = create_fb_device;
   register_driver(di, FB_MAJOR);
   int rc = create_dev_file("fb0", FB_MAJOR, 0 /* minor */, NULL);

   if (rc != 0)
      panic("TTY: unable to create /dev/fb0 (error: %d)", rc);
}

static struct module fb_module = {

   .name = "fb",
   .priority = MOD_fbdev_prio,
   .init = &init_fbdev,
};

REGISTER_MODULE(&fb_module);

