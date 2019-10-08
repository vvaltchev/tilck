/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/process_mm.h>
#include <tilck/kernel/process.h>

user_mapping *
process_add_user_mapping(fs_handle h,
                         void *vaddr,
                         size_t len,
                         size_t off,
                         int prot)
{
   process_info *pi = get_curr_task()->pi;
   user_mapping *um;

   ASSERT((len & OFFSET_IN_PAGE_MASK) == 0);
   ASSERT(!is_preemption_enabled());
   ASSERT(!process_get_user_mapping(vaddr));

   if (!(um = kzmalloc(sizeof(user_mapping))))
      return NULL;

   list_node_init(&um->pi_node);

   um->h = h;
   um->len = len;
   um->vaddrp = vaddr;
   um->off = off;
   um->prot = prot;

   list_add_tail(&pi->mappings, &um->pi_node);
   return um;
}

void process_remove_user_mapping(user_mapping *um)
{
   ASSERT(!is_preemption_enabled());

   list_remove(&um->pi_node);
   kfree2(um, sizeof(user_mapping));
}

user_mapping *process_get_user_mapping(void *vaddrp)
{
   ASSERT(!is_preemption_enabled());

   uptr vaddr = (uptr)vaddrp;
   process_info *pi = get_curr_task()->pi;
   user_mapping *pos;

   list_for_each_ro(pos, &pi->mappings, pi_node) {

      if (IN_RANGE(vaddr, pos->vaddr, pos->vaddr + pos->len))
         return pos;
   }

   return NULL;
}

void remove_all_mappings_of_handle(process_info *pi, fs_handle h)
{
   user_mapping *pos, *temp;

   disable_preemption();
   {
      list_for_each(pos, temp, &pi->mappings, pi_node) {
         if (pos->h == h)
            full_remove_user_mapping(pi, pos);
      }
   }
   enable_preemption();
}

void full_remove_user_mapping(process_info *pi, user_mapping *um)
{
   size_t actual_len = um->len;
   vfs_munmap(um->h, um->vaddrp, actual_len);

   per_heap_kfree(pi->mmap_heap,
                  um->vaddrp,
                  &actual_len,
                  KFREE_FL_ALLOW_SPLIT |
                  KFREE_FL_MULTI_STEP  |
                  KFREE_FL_NO_ACTUAL_FREE);

   process_remove_user_mapping(um);
}


void user_vfree_and_unmap(uptr user_vaddr, size_t page_count)
{
   pdir_t *pdir = get_curr_pdir();
   uptr va = user_vaddr;

   for (size_t i = 0; i < page_count; i++, va += PAGE_SIZE) {

      if (!is_mapped(pdir, (void *)va))
         continue;

      unmap_page(pdir, (void *)va, true);
   }
}

bool user_valloc_and_map_slow(uptr user_vaddr, size_t page_count)
{
   pdir_t *pdir = get_curr_pdir();
   uptr pa, va = user_vaddr;
   void *kernel_vaddr;

   for (size_t i = 0; i < page_count; i++, va += PAGE_SIZE) {

      if (is_mapped(pdir, (void *)va)) {
         user_vfree_and_unmap(user_vaddr, i);
         return false;
      }

      if (!(kernel_vaddr = kmalloc(PAGE_SIZE))) {
         user_vfree_and_unmap(user_vaddr, i);
         return false;
      }

      pa = KERNEL_VA_TO_PA(kernel_vaddr);

      if (map_page(pdir, (void *)va, pa, true, true) != 0) {
         kfree2(kernel_vaddr, PAGE_SIZE);
         user_vfree_and_unmap(user_vaddr, i);
         return false;
      }
   }

   return true;
}

bool user_valloc_and_map(uptr user_vaddr, size_t page_count)
{
   pdir_t *pdir = get_curr_pdir();
   size_t size = (size_t)page_count << PAGE_SHIFT;
   size_t count;

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

void user_unmap_zero_page(uptr user_vaddr, size_t page_count)
{
   pdir_t *pdir = get_curr_pdir();
   unmap_pages(pdir, (void *)user_vaddr, page_count, true);
}

bool user_map_zero_page(uptr user_vaddr, size_t page_count)
{
   pdir_t *pdir = get_curr_pdir();
   size_t count =
      map_zero_pages(pdir, (void *)user_vaddr, page_count, true, true);

   if (count != page_count) {
      user_unmap_zero_page(user_vaddr, count);
   }

   return true;
}
