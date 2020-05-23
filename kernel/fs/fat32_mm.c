/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/errno.h>
#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/fs/fat32.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/process_mm.h>

int fat_ramdisk_mm_fixes(struct fat_hdr *hdr, size_t rd_size)
{
   if (fat_is_first_data_sector_aligned(hdr, PAGE_SIZE))
      return 0; /* nothing to do */

   const u32 used = fat_calculate_used_bytes(hdr);
   pdir_t *const pdir = get_kernel_pdir();
   char *const va_begin = (char *)hdr;
   char *const va_end = va_begin + rd_size;
   VERIFY(rd_size >= used);

   if (rd_size - used < PAGE_SIZE) {
      printk("WARNING: [fat ramdisk] cannot align first data sector\n");
      return -1;
   }

   for (char *va = va_begin; va < va_end; va += PAGE_SIZE)
      set_page_rw(pdir, va, true);

   fat_align_first_data_sector(hdr, PAGE_SIZE);

   for (char *va = va_begin; va < va_end; va += PAGE_SIZE)
      set_page_rw(pdir, va, false);

   printk("[fat ramdisk]: align of ramdisk was necessary\n");
   return 0;
}

int fat_mmap(struct user_mapping *um, bool register_only)
{
   struct process *pi = get_curr_proc();
   struct fatfs_handle *fh = um->h;
   struct fat_fs_device_data *d = fh->fs->device_data;

   if (!d->mmap_support)
      return -ENODEV; /* We do NOT support mmap for this "superblock" */

   if (d->cluster_size < PAGE_SIZE)
      return -ENODEV; /* We do NOT support mmap in this case */

   (void)pi;
   NOT_IMPLEMENTED();
}

int fat_munmap(fs_handle h, void *vaddrp, size_t len)
{
   struct fatfs_handle *fh = h;
   struct fat_fs_device_data *d = fh->fs->device_data;

   if (!d->mmap_support)
      return -ENODEV; /* We do NOT support mmap for this "superblock" */

   if (d->cluster_size < PAGE_SIZE)
      return -ENODEV; /* We do NOT support mmap in this case */

   NOT_IMPLEMENTED();
}
