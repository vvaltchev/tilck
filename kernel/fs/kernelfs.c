/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/errno.h>
#include <tilck/kernel/fs/kernelfs.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/sys_types.h>

/*
 * KernelFS is a special, unmounted, file-system designed for special kernel
 * objects like pipes. It's existence cannot be avoided since all handles must
 * have a valid `fs` pointer.
 *
 * Currently, only pipes use it.
 */

static struct mnt_fs *kernelfs;

static void no_lock(struct mnt_fs *fs) { }

static vfs_inode_ptr_t
kernelfs_get_inode(fs_handle h)
{
   return ((struct kfs_handle *)h)->kobj;
}

static void
kernelfs_on_close(fs_handle h)
{
   struct kfs_handle *kh = h;

   if (kh->kobj->on_handle_close)
      kh->kobj->on_handle_close(kh);
}

static void
kernelfs_on_close_last_handle(fs_handle h)
{
   struct kfs_handle *kh = h;
   kh->kobj->destory_obj(kh->kobj);
}

int
kernelfs_stat(struct mnt_fs *fs, vfs_inode_ptr_t i, struct k_stat64 *statbuf)
{
   NOT_IMPLEMENTED();
}

static int
kernelfs_retain_inode(struct mnt_fs *fs, vfs_inode_ptr_t inode)
{
   return retain_obj((struct kobj_base *)inode);
}

static int
kernelfs_release_inode(struct mnt_fs *fs, vfs_inode_ptr_t inode)
{
   return release_obj((struct kobj_base *)inode);
}

static int
kernelfs_on_dup(fs_handle new_h)
{
   struct kfs_handle *kh = new_h;

   if (kh->kobj->on_handle_dup)
      kh->kobj->on_handle_dup(kh);

   return 0;
}

struct kfs_handle *
kfs_create_new_handle(const struct file_ops *fops,
                      struct kobj_base *kobj,
                      int fl_flags)
{
   struct kfs_handle *h;

   if (!(h = vfs_create_new_handle(kernelfs, fops)))
      return NULL;

   h->kobj = kobj;
   h->fl_flags = fl_flags;
   h->fd_flags = 0;

   /* Retain the object, as, in general, each file-handle retains the inode */
   retain_obj(h->kobj);

   /*
    * Usually, VFS's open() retains the FS, but in this case, there is no open,
    * because we're creating and "opening" the "file" at the same time.
    * Therefore the retain on the FS has to be done here.
    */
   retain_obj(h->fs);
   return h;
}

void
kfs_destroy_handle(struct kfs_handle *h)
{
   if (h->kobj)
      release_obj(h->kobj);

   vfs_free_handle(h);
   release_obj(kernelfs);
}

static const struct fs_ops static_fsops_kernelfs =
{
   /* Implemented by the kernel object (e.g. pipe) */
   .stat = kernelfs_stat,
   .retain_inode = kernelfs_retain_inode,
   .release_inode = kernelfs_release_inode,

   /* Implemented here */
   .on_close = kernelfs_on_close,
   .on_close_last_handle = kernelfs_on_close_last_handle,
   .on_dup_cb = kernelfs_on_dup,
   .get_inode = kernelfs_get_inode,

   .fs_exlock = no_lock,
   .fs_exunlock = no_lock,
   .fs_shlock = no_lock,
   .fs_shunlock = no_lock,
};

static struct mnt_fs *create_kernelfs(void)
{
   struct mnt_fs *fs;

   /* Disallow multiple instances of kernelfs */
   ASSERT(kernelfs == NULL);

   fs = create_fs_obj("kernelfs", &static_fsops_kernelfs, NULL, VFS_FS_RW);

   if (!fs)
      return NULL;

   fs->ref_count = 1;
   return fs;
}

void init_kernelfs(void)
{
   kernelfs = create_kernelfs();

   if (!kernelfs)
      panic("Unable to create kernelfs");
}
