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

static struct fs *kernelfs;

static void no_lock(struct fs *fs) { }

static vfs_inode_ptr_t
kernelfs_get_inode(fs_handle h)
{
   return ((struct kfs_handle *)h)->kobj;
}

static void
kernelfs_close(fs_handle h)
{
   struct kfs_handle *kh = h;

   if (kh->kobj->on_handle_close)
      kh->kobj->on_handle_close(kh);

   if (release_obj(kh->kobj) == 0)
      kh->kobj->destory_obj(kh->kobj);

   kfree2(h, sizeof(struct kfs_handle));
}

int
kernelfs_stat(struct fs *fs, vfs_inode_ptr_t i, struct stat64 *statbuf)
{
   NOT_IMPLEMENTED();
}

static int
kernelfs_retain_inode(struct fs *fs, vfs_inode_ptr_t inode)
{
   return retain_obj((struct kobj_base *)inode);
}

static int kernelfs_release_inode(struct fs *fs, vfs_inode_ptr_t inode)
{
   return release_obj((struct kobj_base *)inode);
}

static int
kernelfs_dup(fs_handle fsh, fs_handle *dup_h)
{
   struct kfs_handle *n;

   if (!(n = (void *)kmalloc(sizeof(struct kfs_handle))))
      return -ENOMEM;

   memcpy(n, fsh, sizeof(struct kfs_handle));
   retain_obj(n->kobj);
   *dup_h = n;

   if (n->kobj->on_handle_dup)
      n->kobj->on_handle_dup(n);

   return 0;
}

struct kfs_handle *
kfs_create_new_handle(const struct file_ops *fops,
                      struct kobj_base *kobj,
                      int fl_flags)
{
   struct kfs_handle *h;

   if (!(h = (void *)kzmalloc(sizeof(struct kfs_handle))))
      return NULL;

   vfs_init_fs_handle_base_fields((void *)h, kernelfs, fops);
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

   kfree2(h, sizeof(struct kfs_handle));
   release_obj(kernelfs);
}

static const struct fs_ops static_fsops_kernelfs =
{
   /* Implemented by the kernel object (e.g. pipe) */
   .stat = kernelfs_stat,
   .retain_inode = kernelfs_retain_inode,
   .release_inode = kernelfs_release_inode,

   /* Implemented here */
   .close = kernelfs_close,
   .dup = kernelfs_dup,
   .get_inode = kernelfs_get_inode,

   .fs_exlock = no_lock,
   .fs_exunlock = no_lock,
   .fs_shlock = no_lock,
   .fs_shunlock = no_lock,
};

static struct fs *create_kernelfs(void)
{
   struct fs *fs;

   /* Disallow multiple instances of kernelfs */
   ASSERT(kernelfs == NULL);

   if (!(fs = kzmalloc(sizeof(struct fs))))
      return NULL;

   fs->ref_count = 1;
   fs->fs_type_name = "kernelfs";
   fs->device_id = vfs_get_new_device_id();
   fs->flags = VFS_FS_RW;
   fs->device_data = NULL;
   fs->fsops = &static_fsops_kernelfs;

   return fs;
}

void init_kernelfs(void)
{
   kernelfs = create_kernelfs();

   if (!kernelfs)
      panic("Unable to create kernelfs");
}
