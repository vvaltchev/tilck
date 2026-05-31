/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/process_mm.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/paging_hw.h>

struct user_mapping *
new_user_mapping(struct list *mappings,
                 enum user_mapping_type type,
                 struct process *pi,
                 fs_handle h,
                 void *vaddr,
                 size_t len,
                 size_t off,
                 int prot)
{
   struct user_mapping *um;

   ASSERT((len & OFFSET_IN_PAGE_MASK) == 0);

   if (!(um = kzalloc_obj(struct user_mapping)))
      return NULL;

   list_node_init(&um->pi_node);
   list_node_init(&um->inode_node);

   um->type = type;
   um->pi = pi;
   um->h = h;
   um->len = len;
   um->vaddrp = vaddr;
   um->off = off;
   um->prot = prot;

   list_add_tail(mappings, &um->pi_node);
   return um;
}

struct user_mapping *
process_add_user_mapping(fs_handle h,
                         void *vaddr,
                         size_t len,
                         size_t off,
                         int prot)
{
   struct process *pi = get_curr_proc();

   ASSERT(!is_preemption_enabled());
   ASSERT(!process_get_user_mapping(vaddr));
   ASSERT(pi->mi);

   return new_user_mapping(&pi->mi->mappings, USER_MAPPING_MMAP,
                           pi, h, vaddr, len, off, prot);
}

void process_remove_user_mapping(struct user_mapping *um)
{
   ASSERT(!is_preemption_enabled());

   list_remove(&um->pi_node);
   list_remove(&um->inode_node);
   kfree_obj(um, struct user_mapping);
}

struct user_mapping *process_get_user_mapping(void *vaddrp)
{
   struct user_mapping *pos;
   const ulong vaddr = (ulong)vaddrp;
   struct process *pi = get_curr_proc();

   ASSERT(!is_preemption_enabled());
   ASSERT(pi->mi);

   /*
    * The list contains the process' base regions (program segments, stack,
    * heap) plus any mmap()ed region. It is supposed to be *short*, so a linear
    * scan is acceptable. If it ever grows large, switch to a BST here.
    */

   list_for_each_ro(pos, &pi->mi->mappings, pi_node) {

      if (IN_RANGE(vaddr, pos->vaddr, pos->vaddr + pos->len))
         return pos;
   }

   return NULL;
}

void remove_all_user_mappings(struct process *pi)
{
   struct user_mapping *um, *tmp;
   struct mappings_info *mi = pi->mi;

   ASSERT(!is_preemption_enabled());
   ASSERT(mi);

   /*
    * Remove every non file-backed mapping: the anonymous mmap()s and the base
    * regions (program segments, stack, heap). File-backed mappings are removed
    * separately when their handle is closed (remove_all_mappings_of_handle /
    * remove_all_file_mappings), so by the time we get here none remain -- hence
    * the final list_is_empty() assertion.
    */

   list_for_each(um, tmp, &mi->mappings, pi_node) {

      if (um->h)
         continue;

      if (um->type == USER_MAPPING_MMAP)
         full_remove_user_mapping(pi, um);   /* anon mmap: lives in mmap_heap */
      else
         process_remove_user_mapping(um);    /* base region: free the struct;
                                              * pages freed by pdir_destroy() */
   }

   ASSERT(list_is_empty(&mi->mappings));
}

void remove_all_mappings_of_handle(struct process *pi, fs_handle h)
{
   struct user_mapping *pos, *temp;
   struct mappings_info *mi = pi->mi;

   if (!mi)
      return;

   disable_preemption();
   {
      list_for_each(pos, temp, &mi->mappings, pi_node) {
         if (pos->h == h)
            full_remove_user_mapping(pi, pos);
      }
   }
   enable_preemption();
}

void full_remove_user_mapping(struct process *pi, struct user_mapping *um)
{
   struct mappings_info *mi = pi->mi;
   size_t actual_len = um->len;

   ASSERT(mi);
   ASSERT(mi->mmap_heap);

   if (um->h)
      vfs_munmap(um, um->vaddrp, actual_len);

   per_heap_kfree(mi->mmap_heap,
                  um->vaddrp,
                  &actual_len,
                  KFREE_FL_ALLOW_SPLIT |
                  KFREE_FL_MULTI_STEP  |
                  KFREE_FL_NO_ACTUAL_FREE);

   process_remove_user_mapping(um);
}

void remove_all_file_mappings(struct process *pi)
{
   fs_handle *h;

   for (u32 i = 0; i < KRN_MAX_HANDLES; i++) {

      if (!(h = pi->handles[i]))
         continue;

      remove_all_mappings_of_handle(pi, h);
   }
}

struct mappings_info *alloc_mappings_info(void)
{
   struct mappings_info *mi;

   if (!(mi = kalloc_obj(struct mappings_info)))
      return NULL;

   list_init(&mi->mappings);
   mi->mmap_heap = NULL;
   mi->mmap_heap_size = 0;
   return mi;
}

void free_mappings_info(struct mappings_info *mi)
{
   struct user_mapping *um, *tmp;

   if (mi->mmap_heap) {
      kmalloc_destroy_heap(mi->mmap_heap);
      kfree2(mi->mmap_heap, kmalloc_get_heap_struct_size());
   }

   list_for_each(um, tmp, &mi->mappings, pi_node) {
      list_remove(&um->pi_node);
      list_remove(&um->inode_node);
      kfree_obj(um, struct user_mapping);
   }

   kfree_obj(mi, struct mappings_info);
}

struct mappings_info *
duplicate_mappings_info(struct process *new_pi, struct mappings_info *mi)
{
   struct user_mapping *um, *um2;
   struct mappings_info *new_mi = NULL;

   if (!(new_mi = kalloc_obj(struct mappings_info)))
      goto oom_case;

   list_init(&new_mi->mappings);
   new_mi->mmap_heap = NULL;
   new_mi->mmap_heap_size = 0;

   if (mi->mmap_heap) {

      if (!(new_mi->mmap_heap = kmalloc_heap_dup(mi->mmap_heap)))
         goto oom_case;

      new_mi->mmap_heap_size = mi->mmap_heap_size;
   }

   list_for_each_ro(um, &mi->mappings, pi_node) {

      if (!(um2 = kalloc_obj(struct user_mapping)))
         goto oom_case;

      /* First just copy the mapping info */
      *um2 = *um;

      /* Re-assign the process pointer */
      um2->pi = new_pi;

      /* Re-init the new nodes */
      list_node_init(&um2->pi_node);
      list_node_init(&um2->inode_node);

      /* Add the pi_node to new process's mappings list */
      list_add_tail(&new_mi->mappings, &um2->pi_node);

      /*
       * If the inode_node belongs to a list (mappings per inode)
       * add the new mapping's inode_node to the same list.
       */
      if (list_is_node_in_list(&um->inode_node))
         list_add_after(&um->inode_node, &um2->inode_node);
   }

   return new_mi;

oom_case:

   if (new_mi) {

      if (new_mi->mmap_heap) {
         kmalloc_destroy_heap(new_mi->mmap_heap);
         kfree2(new_mi->mmap_heap, kmalloc_get_heap_struct_size());
      }

      list_for_each(um, um2, &new_mi->mappings, pi_node) {
         list_remove(&um->pi_node);
         kfree_obj(um, struct user_mapping);
      }

      kfree_obj(new_mi, struct mappings_info);
   }

   return NULL;
}

void user_vfree_and_unmap(ulong user_vaddr, size_t page_count)
{
   ulong va = user_vaddr;
   pdir_t *pdir = get_curr_pdir();

   for (size_t i = 0; i < page_count; i++, va += PAGE_SIZE) {

      if (!is_mapped(pdir, (void *)va))
         continue;

      unmap_page(pdir, (void *)va, true);
   }
}

bool user_valloc_and_map_slow(ulong user_vaddr, size_t page_count)
{
   void *kernel_vaddr;
   ulong pa, va = user_vaddr;
   pdir_t *pdir = get_curr_pdir();

   for (size_t i = 0; i < page_count; i++, va += PAGE_SIZE) {

      if (is_mapped(pdir, (void *)va)) {
         user_vfree_and_unmap(user_vaddr, i);
         return false;
      }

      if (!(kernel_vaddr = kmalloc(PAGE_SIZE))) {
         user_vfree_and_unmap(user_vaddr, i);
         return false;
      }

      pa = LIN_VA_TO_PA(kernel_vaddr);

      if (map_page(pdir, (void *)va, pa, PAGING_FL_RWUS) != 0) {
         kfree2(kernel_vaddr, PAGE_SIZE);
         user_vfree_and_unmap(user_vaddr, i);
         return false;
      }
   }

   return true;
}

bool user_valloc_and_map(ulong user_vaddr, size_t page_count)
{
   size_t count;
   size_t size = (size_t)page_count << PAGE_SHIFT;
   pdir_t *pdir = get_curr_pdir();

   void *kernel_vaddr =
      general_kmalloc(&size, KMALLOC_FL_MULTI_STEP | PAGE_SIZE);

   if (!kernel_vaddr)
      return user_valloc_and_map_slow(user_vaddr, page_count);

   ASSERT(size == (size_t)page_count << PAGE_SHIFT);

   count = map_pages(pdir,
                     (void *)user_vaddr,
                     LIN_VA_TO_PA(kernel_vaddr),
                     page_count,
                     PAGING_FL_US | PAGING_FL_RW);

   if (count != page_count) {
      unmap_pages(pdir, (void *)user_vaddr, count, false);
      general_kfree(kernel_vaddr,
                    &size,
                    KFREE_FL_ALLOW_SPLIT | KFREE_FL_MULTI_STEP);
      return false;
   }

   return true;
}

void user_unmap_zero_page(ulong user_vaddr, size_t page_count)
{
   pdir_t *pdir = get_curr_pdir();
   unmap_pages(pdir, (void *)user_vaddr, page_count, true);
}

bool user_map_zero_page(ulong user_vaddr, size_t page_count)
{
   size_t count = map_zero_pages(get_curr_pdir(),
                                 (void *)user_vaddr,
                                 page_count,
                                 PAGING_FL_US | PAGING_FL_RW);

   if (count != page_count) {
      user_unmap_zero_page(user_vaddr, count);
      return false;
   }

   return true;
}

int generic_fs_munmap(struct user_mapping *um, void *vaddrp, size_t len)
{
   struct fs_handle_base *hb = um->h;
   struct process *pi = hb->pi;
   ulong vaddr = (ulong)vaddrp;
   ulong vend = vaddr + len;

   ASSERT(IS_PAGE_ALIGNED(len));

   for (; vaddr < vend; vaddr += PAGE_SIZE) {
      unmap_page_permissive(pi->pdir, (void *)vaddr, false);
   }

   return 0;
}
