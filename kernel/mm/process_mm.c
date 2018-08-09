
#include <tilck/common/basic_defs.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/errno.h>

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

sptr
sys_mmap_pgoff(void *addr, size_t len, int prot,
               int flags, int fd, size_t pgoffset)
{
   task_info *curr = get_curr_task();
   process_info *pi = curr->pi;

   //printk("mmap2(addr: %p, len: %u, prot: %u, flags: %p, fd: %d, off: %d)\n",
   //      addr, len, prot, flags, fd, pgoffset);

   if (addr != NULL)
      return -EINVAL;

   if (!IS_PAGE_ALIGNED(len))
      return -EINVAL;

   if (flags != (MAP_ANONYMOUS | MAP_PRIVATE))
      return -EINVAL; /* support only anon + private mappings */

   if (fd != -1 || pgoffset != 0)
      return -EINVAL;

   if (prot != (PROT_READ | PROT_WRITE))
      return -EINVAL; /* support only read/write allocs */

   if (!pi->mmap_heap) {

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
   }

   size_t actual_len = len;
   void *res;

   disable_preemption();
   {
      res = per_heap_kmalloc(pi->mmap_heap,
                             &actual_len,
                             KMALLOC_FL_MULTI_STEP | PAGE_SIZE);
   }
   enable_preemption();

   ASSERT(actual_len == round_up_at(len, PAGE_SIZE));

   if (!res)
      return -ENOMEM;

#if MMAP_NO_COW
   bzero(res, actual_len);
#endif

   return (sptr)res;
}

sptr sys_munmap(void *vaddr, size_t len)
{
   task_info *curr = get_curr_task();
   process_info *pi = curr->pi;

   if (!len || !pi->mmap_heap || !IS_PAGE_ALIGNED(len))
      return -EINVAL;

   if ((uptr)vaddr < USER_MMAP_BEGIN || (uptr)vaddr >= USER_MMAP_END)
      return -EINVAL;

   disable_preemption();
   {
      size_t actual_len = len;
      per_heap_kfree(pi->mmap_heap,
                     vaddr,
                     &actual_len,
                     KFREE_FL_ALLOW_SPLIT | KFREE_FL_MULTI_STEP);
      ASSERT(actual_len == round_up_at(len, PAGE_SIZE));
   }
   enable_preemption();
   return 0;
}
