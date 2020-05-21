/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

#include <tilck/kernel/errno.h>
#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/fs/fat32.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/process_mm.h>

int fat_mmap(struct user_mapping *um, bool register_only)
{
   struct process *pi = get_curr_proc();
   struct fatfs_handle *fh = um->h;
   struct fat_fs_device_data *d = fh->fs->device_data;

   if (d->cluster_size < PAGE_SIZE)
      return -ENODEV; /* We do NOT support mmap in this case */

   (void)pi;
   NOT_IMPLEMENTED();
}

int fat_munmap(fs_handle h, void *vaddrp, size_t len)
{
   struct fatfs_handle *fh = h;
   struct fat_fs_device_data *d = fh->fs->device_data;

   if (d->cluster_size < PAGE_SIZE)
      return -ENODEV; /* We do NOT support mmap in this case */

   NOT_IMPLEMENTED();
}
