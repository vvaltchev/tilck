/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_mm.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/process_mm.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/fs/devfs.h>
#include <tilck/kernel/syscalls.h>

#include <sys/mman.h>      // system header

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

      const ulong paddr = LIN_VA_TO_PA(kernel_vaddr);

      if (map_page(pi->pdir, vaddr, paddr, PAGING_FL_RWUS) != 0) {
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
   struct task *ti = get_curr_task();
   struct process *pi = ti->pi;

   if (!new_brk)
      return pi->brk;

   // TODO: check if Linux accepts non-page aligned addresses.
   // If yes, what to do? how to approx? truncation, round-up/round-down?
   if ((ulong)new_brk & OFFSET_IN_PAGE_MASK)
      return pi->brk;

   if (new_brk < pi->initial_brk)
      return pi->brk;

   if ((ulong)new_brk >= MAX_BRK)
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
   struct kmalloc_heap *mmap_heap;
   ASSERT(!pi->mi);

   if (!(pi->mi = kalloc_obj(struct mappings_info)))
      return -ENOMEM;

   if (!(mmap_heap = kzmalloc(kmalloc_get_heap_struct_size()))) {
      kfree_obj(pi->mi, struct mappings_info);
      return -ENOMEM;
   }

   list_init(&pi->mi->mappings);
   pi->mi->mmap_heap = mmap_heap;
   pi->mi->mmap_heap_size = USER_MMAP_MIN_SZ;

   bool success =
      kmalloc_create_heap(mmap_heap,
                          USER_MMAP_BEGIN,
                          pi->mi->mmap_heap_size,
                          PAGE_SIZE,
                          PAGE_SIZE,            /* alloc block size */
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
   per_heap_kfree(pi->mi->mmap_heap,
                  ptr,
                  &actual_len,
                  KFREE_FL_ALLOW_SPLIT |
                  KFREE_FL_MULTI_STEP  |
                  KFREE_FL_NO_ACTUAL_FREE);
}

static struct user_mapping *
mmap_on_user_heap(struct process *pi,
                  size_t *actual_len_ref,
                  fs_handle handle,
                  u32 per_heap_kmalloc_flags,
                  size_t off,
                  int prot)
{
   void *res;
   struct user_mapping *um;

   while (true) {

      struct kmalloc_heap *new_heap;
      struct kmalloc_heap *h = pi->mi->mmap_heap;
      size_t heap_sz = pi->mi->mmap_heap_size;

      res = per_heap_kmalloc(h,
                             actual_len_ref,
                             per_heap_kmalloc_flags);

      if (LIKELY(res != NULL))
         break;        /* great! */

      if (heap_sz == USER_MMAP_MAX_SZ)
         return NULL; /* cannot expand the heap more than that */

      new_heap = kmalloc_heap_dup_expanded(h, heap_sz * 2);

      if (!new_heap)
         return NULL; /* no enough memory */

      pi->mi->mmap_heap_size = heap_sz * 2;
      pi->mi->mmap_heap = new_heap;
      kmalloc_destroy_heap(h);
   }

   /* NOTE: here `handle` might be NULL (zero-map case) and that's OK */
   um = process_add_user_mapping(handle, res, *actual_len_ref, off, prot);

   if (!um) {
      mmap_err_case_free(pi, res, *actual_len_ref);
      return NULL;
   }

   return um;
}

long
sys_mmap_pgoff(void *addr, size_t len, int prot,
               int flags, int fd, size_t pgoffset)
{
   u32 per_heap_kmalloc_flags = KMALLOC_FL_MULTI_STEP | PAGE_SIZE;
   struct task *curr = get_curr_task();
   struct process *pi = curr->pi;
   struct fs_handle_base *handle = NULL;
   struct user_mapping *um = NULL;
   size_t actual_len;
   int rc, fl;

   if ((flags & MAP_PRIVATE) && (flags & MAP_SHARED))
      return -EINVAL; /* non-sense parameters */

   if (!len)
      return -EINVAL;

   if (addr)
      return -EINVAL; /* addr != NULL not supported */

   if (!(prot & PROT_READ))
      return -EINVAL;

   actual_len = pow2_round_up_at(len, PAGE_SIZE);

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
         return -EINVAL; /* nor read nor write prot */

      if ((prot & (PROT_READ | PROT_WRITE)) == PROT_WRITE)
         return -EINVAL; /* disallow write-only mappings */

      if (prot & PROT_WRITE) {
         if (!(fl & O_WRONLY) && (fl & O_RDWR) != O_RDWR)
            return -EACCES;
      }

      per_heap_kmalloc_flags |= KMALLOC_FL_NO_ACTUAL_ALLOC;
   }

   if (!pi->mi) {
      if ((rc = create_process_mmap_heap(pi))) {
         return rc;
      }
   }

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

   ASSERT(actual_len == pow2_round_up_at(len, PAGE_SIZE));

   if (handle) {

      if ((rc = vfs_mmap(um, pi->pdir, 0))) {

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

   return (long)um->vaddr;
}

static int munmap_int(struct process *pi, void *vaddrp, size_t len)
{
   u32 kfree_flags = KFREE_FL_ALLOW_SPLIT | KFREE_FL_MULTI_STEP;
   struct user_mapping *um = NULL, *um2 = NULL;
   ulong vaddr = (ulong) vaddrp;
   size_t actual_len;
   int rc;

   ASSERT(!is_preemption_enabled());

   actual_len = pow2_round_up_at(len, PAGE_SIZE);
   um = process_get_user_mapping(vaddrp);

   if (!um) {

      /*
       * We just don't have any user_mappings containing [vaddrp, vaddrp+len).
       * Just ignore that and return 0 [linux behavior].
       */

      printk("[%d] Un-map unknown chunk at [%p, %p)\n",
             pi->pid, TO_PTR(vaddr), TO_PTR(vaddr + actual_len));
      return 0;
   }

   const ulong um_vend = um->vaddr + um->len;

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

         /* Shrink the current struct user_mapping */
         um->len = vaddr - um->vaddr;

         /* Create a new struct user_mapping for its 2nd part */
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
      rc = vfs_munmap(um, vaddrp, actual_len);

      /*
       * If there's an actual user_mapping entry, it means um->h's fops MUST
       * HAVE mmap() implemented. Therefore, we MUST REQUIRE munmap() to be
       * present as well.
       */

      ASSERT(rc != -ENODEV);
      (void) rc; /* prevent the "unused variable" Werror in release */

      if (um2)
         vfs_mmap(um2, pi->pdir, VFS_MM_DONT_MMAP);
   }

   per_heap_kfree(pi->mi->mmap_heap,
                  vaddrp,
                  &actual_len,
                  kfree_flags);

   ASSERT(actual_len == pow2_round_up_at(len, PAGE_SIZE));
   return 0;
}

int sys_munmap(void *vaddrp, size_t len)
{
   struct task *curr = get_curr_task();
   struct process *pi = curr->pi;
   ulong vaddr = (ulong) vaddrp;
   int rc;

   if (!len || !pi->mi->mmap_heap)
      return -EINVAL;

   if (!IN_RANGE(vaddr,
                 USER_MMAP_BEGIN,
                 USER_MMAP_BEGIN + pi->mi->mmap_heap_size))
   {
      return -EINVAL;
   }

   disable_preemption();
   {
      rc = munmap_int(pi, vaddrp, len);
   }
   enable_preemption();
   return rc;
}
