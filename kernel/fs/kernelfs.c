/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/sys_types.h>

static struct fs *kernelfs;

static void no_lock(struct fs *fs) { }

static vfs_inode_ptr_t
kernelfs_get_inode(fs_handle h)
{
   NOT_IMPLEMENTED();
}

static void
kernelfs_close(fs_handle h)
{
   NOT_IMPLEMENTED();
}

int
kernelfs_stat(struct fs *fs, vfs_inode_ptr_t i, struct stat64 *statbuf)
{
   NOT_IMPLEMENTED();
}

static int
kernelfs_dup(fs_handle fsh, fs_handle *dup_h)
{
   NOT_IMPLEMENTED();
}

static int
kernelfs_retain_inode(struct fs *fs, vfs_inode_ptr_t inode)
{
   NOT_IMPLEMENTED();
}

static int kernelfs_release_inode(struct fs *fs, vfs_inode_ptr_t inode)
{
   NOT_IMPLEMENTED();
}


static const struct fs_ops static_fsops_kernelfs =
{
   .get_inode = kernelfs_get_inode,
   .close = kernelfs_close,
   .stat = kernelfs_stat,
   .dup = kernelfs_dup,

   .retain_inode = kernelfs_retain_inode,
   .release_inode = kernelfs_release_inode,
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
