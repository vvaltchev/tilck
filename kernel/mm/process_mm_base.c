/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/process_mm.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/paging_hw.h>

struct user_mapping *
process_add_user_mapping(fs_handle h,
                         void *vaddr,
                         size_t len,
                         size_t off,
                         int prot)
{
   struct process *pi = get_curr_proc();
   struct user_mapping *um;

   ASSERT((len & OFFSET_IN_PAGE_MASK) == 0);
   ASSERT(!is_preemption_enabled());
   ASSERT(!process_get_user_mapping(vaddr));
   ASSERT(pi->mi);

   if (!(um = kzmalloc(sizeof(struct user_mapping))))
      return NULL;

   list_node_init(&um->pi_node);
   list_node_init(&um->inode_node);

   um->pi = pi;
   um->h = h;
   um->len = len;
   um->vaddrp = vaddr;
   um->off = off;
   um->prot = prot;

   list_add_tail(&pi->mi->mappings, &um->pi_node);
   return um;
}

void process_remove_user_mapping(struct user_mapping *um)
{
   ASSERT(!is_preemption_enabled());

   list_remove(&um->pi_node);
   list_remove(&um->inode_node);
   kfree2(um, sizeof(struct user_mapping));
}

struct user_mapping *process_get_user_mapping(void *vaddrp)
{
   const ulong vaddr = (ulong)vaddrp;
   struct process *pi = get_curr_proc();
   struct user_mapping *pos;

   ASSERT(!is_preemption_enabled());

   if (!pi->mi)
      return NULL;

   list_for_each_ro(pos, &pi->mi->mappings, pi_node) {

      if (IN_RANGE(vaddr, pos->vaddr, pos->vaddr + pos->len))
         return pos;
   }

   return NULL;
}

void remove_all_user_zero_mem_mappings(struct process *pi)
{
   struct user_mapping *um;
   struct list *mappings_list_p;

   ASSERT(!is_preemption_enabled());

   if (!pi->mi)
      return;

   mappings_list_p = &pi->mi->mappings;

   list_for_each_ro(um, mappings_list_p, pi_node) {

      if (!um->h)
         full_remove_user_mapping(pi, um);
   }

   ASSERT(list_is_empty(mappings_list_p));
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
      vfs_munmap(um->h, um->vaddrp, actual_len);

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

   for (u32 i = 0; i < MAX_HANDLES; i++) {

      if (!(h = pi->handles[i]))
         continue;

      remove_all_mappings_of_handle(pi, h);
   }
}

struct mappings_info *
duplicate_mappings_info(struct mappings_info *mi)
{
   struct mappings_info *new_mi = NULL;
   struct user_mapping *um, *um2;

   if (!(new_mi = kmalloc(sizeof(struct mappings_info))))
      goto oom_case;

   if (!(new_mi->mmap_heap = kmalloc_heap_dup(mi->mmap_heap)))
      goto oom_case;

   list_init(&new_mi->mappings);

   list_for_each_ro(um, &mi->mappings, pi_node) {

      if (!(um2 = kmalloc(sizeof(struct user_mapping))))
         goto oom_case;

      *um2 = *um;
      list_node_init(&um2->pi_node);
      list_node_init(&um2->inode_node);

      list_add_tail(&new_mi->mappings, &um2->pi_node);

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
         kfree2(um, sizeof(struct user_mapping));
      }

      kfree2(new_mi, sizeof(struct mappings_info));
   }

   return NULL;
}

void user_vfree_and_unmap(ulong user_vaddr, size_t page_count)
{
   pdir_t *pdir = get_curr_pdir();
   ulong va = user_vaddr;

   for (size_t i = 0; i < page_count; i++, va += PAGE_SIZE) {

      if (!is_mapped(pdir, (void *)va))
         continue;

      unmap_page(pdir, (void *)va, true);
   }
}

bool user_valloc_and_map_slow(ulong user_vaddr, size_t page_count)
{
   pdir_t *pdir = get_curr_pdir();
   ulong pa, va = user_vaddr;
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

bool user_valloc_and_map(ulong user_vaddr, size_t page_count)
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

void user_unmap_zero_page(ulong user_vaddr, size_t page_count)
{
   pdir_t *pdir = get_curr_pdir();
   unmap_pages(pdir, (void *)user_vaddr, page_count, true);
}

bool user_map_zero_page(ulong user_vaddr, size_t page_count)
{
   pdir_t *pdir = get_curr_pdir();
   size_t count =
      map_zero_pages(pdir, (void *)user_vaddr, page_count, true, true);

   if (count != page_count) {
      user_unmap_zero_page(user_vaddr, count);
   }

   return true;
}

int generic_fs_munmap(fs_handle h, void *vaddrp, size_t len)
{
   struct fs_handle_base *hb = h;
   struct process *pi = hb->pi;
   ulong vaddr = (ulong)vaddrp;
   ulong vend = vaddr + len;
   ASSERT(IS_PAGE_ALIGNED(len));

   for (; vaddr < vend; vaddr += PAGE_SIZE) {
      unmap_page_permissive(pi->pdir, (void *)vaddr, false);
   }

   return 0;
}
