/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/fs/devfs.h>

page_directory_t *kernel_page_dir;
page_directory_t *curr_page_dir;
char page_size_buf[PAGE_SIZE] ALIGNED_AT(PAGE_SIZE);

void user_vfree_and_unmap(uptr user_vaddr, int page_count)
{
   page_directory_t *pdir = get_curr_pdir();
   uptr va = user_vaddr;

   for (int i = 0; i < page_count; i++, va += PAGE_SIZE) {

      if (!is_mapped(pdir, (void *)va))
         continue;

      unmap_page(pdir, (void *)va, true);
   }
}

bool user_valloc_and_map_slow(uptr user_vaddr, int page_count)
{
   page_directory_t *pdir = get_curr_pdir();
   uptr va = user_vaddr;

   for (int i = 0; i < page_count; i++, va += PAGE_SIZE) {

      if (is_mapped(pdir, (void *)va)) {
         user_vfree_and_unmap(user_vaddr, i);
         return false;
      }

      void *kernel_vaddr = kmalloc(PAGE_SIZE);

      if (!kernel_vaddr) {
         user_vfree_and_unmap(user_vaddr, i);
         return false;
      }

      uptr pa = KERNEL_VA_TO_PA(kernel_vaddr);
      int rc = map_page(pdir, (void *)va, pa, true, true);

      if (rc != 0) {
         kfree2(kernel_vaddr, PAGE_SIZE);
         user_vfree_and_unmap(user_vaddr, i);
         return false;
      }
   }

   return true;
}

bool user_valloc_and_map(uptr user_vaddr, int page_count)
{
   page_directory_t *pdir = get_curr_pdir();
   size_t size = page_count * PAGE_SIZE;
   int count;

   void *kernel_vaddr =
      general_kmalloc(&size, KMALLOC_FL_MULTI_STEP | PAGE_SIZE);

   if (!kernel_vaddr)
      return user_valloc_and_map_slow(user_vaddr, page_count);

   ASSERT(size == (size_t)page_count << PAGE_SHIFT);

   count = map_pages(pdir,
                     (void *)user_vaddr,
                     KERNEL_VA_TO_PA(kernel_vaddr),
                     page_count,
                     false,
                     true, true);

   if (count != page_count) {
      unmap_pages(pdir, (void *)user_vaddr, count, false);
      general_kfree(kernel_vaddr,
                    &size,
                    KFREE_FL_ALLOW_SPLIT | KFREE_FL_MULTI_STEP);
      return false;
   }

   return true;
}

void user_unmap_zero_page(uptr user_vaddr, int page_count)
{
   page_directory_t *pdir = get_curr_pdir();
   unmap_pages(pdir, (void *)user_vaddr, page_count, true);
}

bool user_map_zero_page(uptr user_vaddr, int page_count)
{
   page_directory_t *pdir = get_curr_pdir();
   int count = map_zero_pages(pdir, (void *)user_vaddr, page_count, true, true);

   if (count != page_count) {
      user_unmap_zero_page(user_vaddr, count);
   }

   return true;
}

sptr sys_brk(void *new_brk)
{
   task_info *ti = get_curr_task();
   process_info *pi = ti->pi;

   if (!new_brk)
      goto ret;

   // TODO: check if Linux accepts non-page aligned addresses.
   // If yes, what to do? how to approx? truncation, round-up/round-down?
   if ((uptr)new_brk & OFFSET_IN_PAGE_MASK)
      goto ret;

   if (new_brk < pi->initial_brk)
      goto ret;

   if ((uptr)new_brk >= MAX_BRK)
      goto ret;

   if (new_brk == pi->brk)
      goto ret;

   disable_preemption();

   /*
    * Disable preemption to avoid any threads to mess-up with the address space
    * of the current process (i.e. they might call brk(), mmap() etc.)
    */

   if (new_brk < pi->brk) {

      /* we have to free pages */

      for (void *vaddr = new_brk; vaddr < pi->brk; vaddr += PAGE_SIZE) {
         unmap_page(pi->pdir, vaddr, true);
      }

      pi->brk = new_brk;
      goto out;
   }

   void *vaddr = pi->brk;

   while (vaddr < new_brk) {

      if (is_mapped(pi->pdir, vaddr))
         goto out; // error: vaddr is already mapped!

      vaddr += PAGE_SIZE;
   }

   /* OK, everything looks good here */

   vaddr = pi->brk;

   while (vaddr < new_brk) {

      void *kernel_vaddr = kmalloc(PAGE_SIZE);

      if (!kernel_vaddr)
         break; /* we've allocated as much as possible */

      const uptr paddr = KERNEL_VA_TO_PA(kernel_vaddr);
      int rc = map_page(pi->pdir, vaddr, paddr, true, true);

      if (rc != 0) {
         kfree2(kernel_vaddr, PAGE_SIZE);
         break;
      }

      vaddr += PAGE_SIZE;
   }

   /* We're done. */
   pi->brk = vaddr;

out:
   enable_preemption();
ret:
   return (sptr)pi->brk;
}

#define PROT_NONE       0x0             /* Page can not be accessed.  */
#define PROT_READ       0x1             /* Page can be read.  */
#define PROT_WRITE      0x2             /* Page can be written.  */
#define PROT_EXEC       0x4             /* Page can be executed.  */

#define MAP_SHARED      0x01
#define MAP_PRIVATE     0x02
#define MAP_ANONYMOUS   0x20

static int create_process_mmap_heap(process_info *pi)
{
   pi->mmap_heap = kzmalloc(kmalloc_get_heap_struct_size());

   if (!pi->mmap_heap)
      return -ENOMEM;

   bool success =
      kmalloc_create_heap(pi->mmap_heap,
                          USER_MMAP_BEGIN,
                          USER_MMAP_END - USER_MMAP_BEGIN,
                          PAGE_SIZE,
                          KMALLOC_MAX_ALIGN,    /* alloc block size */
                          false,                /* linear mapping */
                          NULL,                 /* metadata_nodes */
#if MMAP_NO_COW
                          user_valloc_and_map,
                          user_vfree_and_unmap);
#else
                          user_map_zero_page,
                          user_unmap_zero_page);
#endif

   if (!success)
      return -ENOMEM;

   return 0;
}

sptr
sys_mmap_pgoff(void *addr, size_t len, int prot,
               int flags, int fd, size_t pgoffset)
{
   task_info *curr = get_curr_task();
   process_info *pi = curr->pi;
   fs_handle_base *handle = NULL;
   devfs_file_handle *devfs_handle = NULL;
   u32 per_heap_kmalloc_flags = KMALLOC_FL_MULTI_STEP | PAGE_SIZE;
   user_mapping *um = NULL;
   size_t actual_len;
   void *res;
   int rc;

   //printk("mmap2(addr: %p, len: %u, prot: %u, flags: %p, fd: %d, off: %d)\n",
   //      addr, len, prot, flags, fd, pgoffset);

   if ((flags & MAP_PRIVATE) && (flags & MAP_SHARED))
      return -EINVAL; /* non-sense parameters */

   if (!len)
      return -EINVAL;

   if (addr)
      return -EINVAL; /* addr != NULL not supported */

   if (pgoffset != 0)
      return -EINVAL; /* pgoffset != 0 not supported at the moment */

   if (prot != (PROT_READ | PROT_WRITE))
      return -EINVAL; /* support only read/write mapping, for the moment */

   actual_len = round_up_at(len, PAGE_SIZE);

   if (fd == -1) {

      if (!(flags & MAP_ANONYMOUS))
         return -EINVAL;

      if (flags & MAP_SHARED)
         return -EINVAL; /* MAP_SHARED not supported for anonymous mappings */

      if (!(flags & MAP_PRIVATE))
         return -EINVAL;

   } else {

      if (!(flags & MAP_SHARED))
         return -EINVAL;

      disable_preemption();
      {
         handle = get_fs_handle(fd);
      }
      enable_preemption();

      if (!handle)
         return -EBADF;

      if (handle->fs != get_devfs())
         return -ENODEV; /* only special dev files can be memory-mapped */

      devfs_handle = (devfs_file_handle *) handle;

      if (!devfs_handle->fops.mmap)
         return -ENODEV; /* this device file does not support memory mapping */

      ASSERT(devfs_handle->fops.munmap);
      per_heap_kmalloc_flags |= KMALLOC_FL_NO_ACTUAL_ALLOC;
   }

   if (!pi->mmap_heap)
      if ((rc = create_process_mmap_heap(pi)))
         return rc;

   disable_preemption();
   {
      res = per_heap_kmalloc(pi->mmap_heap,
                             &actual_len,
                             per_heap_kmalloc_flags);

      if (devfs_handle) {

         um = process_add_user_mapping(devfs_handle, res, actual_len);

         if (!um) {
            per_heap_kfree(pi->mmap_heap,
                           res,
                           &actual_len,
                           KFREE_FL_ALLOW_SPLIT |
                           KFREE_FL_MULTI_STEP  |
                           KFREE_FL_NO_ACTUAL_FREE);
            return -ENOMEM;
         }
      }
   }
   enable_preemption();

   ASSERT(actual_len == round_up_at(len, PAGE_SIZE));

   if (!res)
      return -ENOMEM;


   if (devfs_handle) {


      if ((rc = devfs_handle->fops.mmap(handle, res, actual_len))) {

         /*
         * Everything was apparently OK and the allocation in the user virtual
         * address space succeeded, but for some reason the actual mapping of
         * the device to the user vaddr failed.
         */

         per_heap_kfree(pi->mmap_heap,
                        res,
                        &actual_len,
                        KFREE_FL_ALLOW_SPLIT |
                        KFREE_FL_MULTI_STEP  |
                        KFREE_FL_NO_ACTUAL_FREE);

         process_remove_user_mapping(um);
         return rc;
      }


   } else {

      if (MMAP_NO_COW)
         bzero(res, actual_len);
   }

   return (sptr)res;
}

sptr sys_munmap(void *vaddr, size_t len)
{
   task_info *curr = get_curr_task();
   process_info *pi = curr->pi;
   size_t actual_len;
   u32 kfree_flags = KFREE_FL_ALLOW_SPLIT | KFREE_FL_MULTI_STEP;

   if (!len || !pi->mmap_heap)
      return -EINVAL;

   if ((uptr)vaddr < USER_MMAP_BEGIN || (uptr)vaddr >= USER_MMAP_END)
      return -EINVAL;

   // TODO: check if vaddr is a device-mapping. In case it is, pass also the
   // KFREE_FL_NO_ACTUAL_FREE flag to per_heap_kfree().

   disable_preemption();
   {
      actual_len = round_up_at(len, PAGE_SIZE);

      user_mapping *um = process_get_user_mapping(vaddr);

      if (um) {
         kfree_flags |= KFREE_FL_NO_ACTUAL_FREE;
         fs_handle_base *hb = um->h;
         hb->fops.munmap(hb, um->vaddr, actual_len);
         process_remove_user_mapping(um);
      }

      per_heap_kfree(pi->mmap_heap,
                     vaddr,
                     &actual_len,
                     kfree_flags);

      ASSERT(actual_len == round_up_at(len, PAGE_SIZE));
   }
   enable_preemption();
   return 0;
}
