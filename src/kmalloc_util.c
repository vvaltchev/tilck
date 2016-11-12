
#include <paging.h>
#include <stringUtil.h>

bool kbasic_virtual_alloc(uintptr_t vaddr, int pageCount)
{
   ASSERT(!(vaddr & 4095)); // the vaddr must be page-aligned


   page_directory_t *pdir = get_kernel_page_dir();

   // Ensure that we have enough physical memory.
   if (get_free_physical_pages_count() < pageCount) {
      return false;
   }

#ifdef DEBUG
   for (int i = 0; i < pageCount; i++) {
      if (is_mapped(pdir, vaddr + (i << 12))) {
         return false;
      }
   }
#endif

   for (int i = 0; i < pageCount; i++) {

      void *paddr = alloc_phys_page();
      ASSERT(paddr != NULL);

      map_page(pdir, vaddr + (i << 12), (uintptr_t)paddr, false, true);
   }

   return true;
}

bool kbasic_virtual_free(uintptr_t vaddr, int pageCount)
{
   ASSERT(!(vaddr & 4095)); // the vaddr must be page-aligned

   page_directory_t *pdir = get_kernel_page_dir();

   //printk("Free %i pages at %p\n", pageCount, vaddr);

#ifdef DEBUG
   for (int i = 0; i < pageCount; i++) {
      if (!is_mapped(pdir, vaddr + (i << 12))) {
         return false;
      }
   }
#endif

   for (int i = 0; i < pageCount; i++) {

      uintptr_t va = vaddr + (i << 12);

      void *paddr = get_mapping(pdir, va);

      // un-map the virtual address.
      unmap_page(pdir, va);

      // free the physical page as well.
      free_phys_page(paddr);
   }

   return true;
}

