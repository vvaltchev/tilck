/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/process_mm.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/fs/devfs.h>
#include <tilck/kernel/syscalls.h>

#include <sys/mman.h>      // system header

pdir_t *kernel_page_dir;
char page_size_buf[PAGE_SIZE] ALIGNED_AT(PAGE_SIZE);

static inline void sys_brk_internal(struct process *pi, void *new_brk)
{
   ASSERT(!is_preemption_enabled());

   if (new_brk < pi->brk) {

      /* we have to free pages */

      for (void *vaddr = new_brk; vaddr < pi->brk; vaddr += PAGE_SIZE) {
         unmap_page(pi->pdir, vaddr, true);
      }

      pi->brk = new_brk;
      return;
   }

   void *vaddr = pi->brk;

   while (vaddr < new_brk) {

      if (is_mapped(pi->pdir, vaddr))
         return; // error: vaddr is already mapped!

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
}

void *sys_brk(void *new_brk)
{
   struct task_info *ti = get_curr_task();
   struct process *pi = ti->pi;

   if (!new_brk)
      return pi->brk;

   // TODO: check if Linux accepts non-page aligned addresses.
   // If yes, what to do? how to approx? truncation, round-up/round-down?
   if ((uptr)new_brk & OFFSET_IN_PAGE_MASK)
      return pi->brk;

   if (new_brk < pi->initial_brk)
      return pi->brk;

   if ((uptr)new_brk >= MAX_BRK)
      return pi->brk;

   if (new_brk == pi->brk)
      return pi->brk;

   /*
    * Disable preemption to avoid any threads to mess-up with the address space
    * of the current process (i.e. they might call brk(), mmap() etc.)
    */

   disable_preemption();
   {
      sys_brk_internal(pi, new_brk);
   }
   enable_preemption();
   return pi->brk;
}

static int create_process_mmap_heap(struct process *pi)
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

static inline void
mmap_err_case_free(struct process *pi, void *ptr, size_t actual_len)
{
   per_heap_kfree(pi->mmap_heap,
                  ptr,
                  &actual_len,
                  KFREE_FL_ALLOW_SPLIT |
                  KFREE_FL_MULTI_STEP  |
                  KFREE_FL_NO_ACTUAL_FREE);
}

static user_mapping *
mmap_on_user_heap(struct process *pi,
                  size_t *actual_len_ref,
                  fs_handle handle,
                  u32 per_heap_kmalloc_flags,
                  size_t off,
                  int prot)
{
   void *res;
   user_mapping *um;

   res = per_heap_kmalloc(pi->mmap_heap,
                          actual_len_ref,
                          per_heap_kmalloc_flags);

   if (!res)
      return NULL;

   /* NOTE: here `handle` might be NULL (zero-map case) and that's OK */
   um = process_add_user_mapping(handle, res, *actual_len_ref, off, prot);

   if (!um) {
      mmap_err_case_free(pi, res, *actual_len_ref);
      return NULL;
   }

   return um;
}

sptr
sys_mmap_pgoff(void *addr, size_t len, int prot,
               int flags, int fd, size_t pgoffset)
{
   u32 per_heap_kmalloc_flags = KMALLOC_FL_MULTI_STEP | PAGE_SIZE;
   struct task_info *curr = get_curr_task();
   struct process *pi = curr->pi;
   fs_handle_base *handle = NULL;
   user_mapping *um = NULL;
   size_t actual_len;
   int rc, fl;

   //printk("mmap2(addr: %p, len: %u, prot: %u, flags: %p, fd: %d, off: %d)\n",
   //      addr, len, prot, flags, fd, pgoffset);

   if ((flags & MAP_PRIVATE) && (flags & MAP_SHARED))
      return -EINVAL; /* non-sense parameters */

   if (!len)
      return -EINVAL;

   if (addr)
      return -EINVAL; /* addr != NULL not supported */

   if (!(prot & PROT_READ))
      return -EINVAL;

   actual_len = round_up_at(len, PAGE_SIZE);

   if (fd == -1) {

      if (!(flags & MAP_ANONYMOUS))
         return -EINVAL;

      if (flags & MAP_SHARED)
         return -EINVAL; /* MAP_SHARED not supported for anonymous mappings */

      if (!(flags & MAP_PRIVATE))
         return -EINVAL;

      if ((prot & (PROT_READ | PROT_WRITE)) != (PROT_READ | PROT_WRITE))
         return -EINVAL;

      if (pgoffset != 0)
         return -EINVAL; /* pgoffset != 0 does not make sense here */

   } else {

      if (!(flags & MAP_SHARED))
         return -EINVAL;

      handle = get_fs_handle(fd);

      if (!handle)
         return -EBADF;

      fl = handle->fl_flags;

      if ((prot & (PROT_READ | PROT_WRITE)) == 0)
         return -EINVAL;

      if ((prot & (PROT_READ | PROT_WRITE)) == PROT_WRITE)
         return -EINVAL; /* disallow write-only mappings */

      if (prot & PROT_WRITE) {
         if (!(fl & O_WRONLY) && (fl & O_RDWR) != O_RDWR)
            return -EINVAL;
      }

      per_heap_kmalloc_flags |= KMALLOC_FL_NO_ACTUAL_ALLOC;
   }

   if (!pi->mmap_heap)
      if ((rc = create_process_mmap_heap(pi)))
         return rc;

   disable_preemption();
   {
      um = mmap_on_user_heap(pi,
                             &actual_len,
                             handle,
                             per_heap_kmalloc_flags,
                             pgoffset << PAGE_SHIFT,
                             prot);
   }
   enable_preemption();

   if (!um)
      return -ENOMEM;

   ASSERT(actual_len == round_up_at(len, PAGE_SIZE));

   if (handle) {

      if ((rc = vfs_mmap(um, false))) {

         /*
          * Everything was apparently OK and the allocation in the user virtual
          * address space succeeded, but for some reason the actual mapping of
          * the device to the user vaddr failed.
          */

         disable_preemption();
         {
            mmap_err_case_free(pi, um->vaddrp, actual_len);
            process_remove_user_mapping(um);
         }
         enable_preemption();
         return rc;
      }


   } else {

      if (MMAP_NO_COW)
         bzero(um->vaddrp, actual_len);
   }

   return (sptr)um->vaddr;
}

static int munmap_int(struct process *pi, void *vaddrp, size_t len)
{
   u32 kfree_flags = KFREE_FL_ALLOW_SPLIT | KFREE_FL_MULTI_STEP;
   user_mapping *um = NULL, *um2 = NULL;
   uptr vaddr = (uptr) vaddrp;
   size_t actual_len;
   int rc;

   ASSERT(!is_preemption_enabled());

   actual_len = round_up_at(len, PAGE_SIZE);
   um = process_get_user_mapping(vaddrp);

   if (!um) {

      /*
       * We just don't have any user_mappings containing [vaddrp, vaddrp+len).
       * Just ignore that and return 0 [linux behavior].
       */

      printk("[%d] Un-map unknown chunk at [%p, %p)\n",
             pi->pid, vaddr, vaddr + actual_len);
      return 0;
   }

   const uptr um_vend = um->vaddr + um->len;

   if (actual_len == um->len) {

      process_remove_user_mapping(um);

   } else {

      /* partial un-map */

      if (vaddr == um->vaddr) {

         /* unmap the beginning of the chunk */
         um->vaddr += actual_len;
         um->off += actual_len;
         um->len -= actual_len;

      } else if (vaddr + actual_len == um_vend) {

         /* unmap the end of the chunk */
         um->len -= actual_len;

      } else {

         /* Unmap something at the middle of the chunk */

         /* Shrink the current user_mapping */
         um->len = vaddr - um->vaddr;

         /* Create a new user_mapping for its 2nd part */
         um2 = process_add_user_mapping(
            um->h,
            (void *)(vaddr + actual_len),
            (um_vend - (vaddr + actual_len)),
            um->off + um->len + actual_len,
            um->prot
         );

         if (!um2) {

            /*
             * Oops, we're out-of-memory! No problem, revert um->page_count
             * and return -ENOMEM. Linux is allowed to do that.
             */
            um->len = um_vend - um->vaddr;
            return -ENOMEM;
         }
      }
   }

   if (um->h) {

      kfree_flags |= KFREE_FL_NO_ACTUAL_FREE;
      rc = vfs_munmap(um->h, vaddrp, actual_len);

      /*
       * If there's an actual user_mapping entry, it means um->h's fops MUST
       * HAVE mmap() implemented. Therefore, we MUST REQUIRE munmap() to be
       * present as well.
       */

      ASSERT(rc != -ENODEV);
      (void) rc; /* prevent the "unused variable" Werror in release */

      if (um2)
         vfs_mmap(um2, true);
   }

   per_heap_kfree(pi->mmap_heap,
                  vaddrp,
                  &actual_len,
                  kfree_flags);

   ASSERT(actual_len == round_up_at(len, PAGE_SIZE));
   return 0;
}

int sys_munmap(void *vaddrp, size_t len)
{
   struct task_info *curr = get_curr_task();
   struct process *pi = curr->pi;
   uptr vaddr = (uptr) vaddrp;
   int rc;

   if (!len || !pi->mmap_heap)
      return -EINVAL;

   if (vaddr < USER_MMAP_BEGIN || vaddr >= USER_MMAP_END)
      return -EINVAL;

   disable_preemption();
   {
      rc = munmap_int(pi, vaddrp, len);
   }
   enable_preemption();
   return rc;
}
