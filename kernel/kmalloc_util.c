
#include <paging.h>
#include <string_util.h>

bool kbasic_virtual_alloc(uptr vaddr, int page_count)
{
   ASSERT(!(vaddr & (PAGE_SIZE - 1))); // the vaddr must be page-aligned

   page_directory_t *pdir = get_kernel_page_dir();

   // Ensure that we have enough physical memory.
   if (get_free_pageframes_count() < page_count) {
      return false;
   }

   VERIFY(page_count == 32 || page_count == 8);

   if (page_count == 32) {

      uptr paddr = alloc_32_pageframes();

      if (paddr != 0) {
         map_pages(pdir, (void *)vaddr, paddr, 32, false, true);
         return true;
      }

      // For testing purposes, let's check that alloc_32 reasonably almost
      // always succeeds.
      // TODO: remove the NOT_REACHED below.
      NOT_REACHED();
   }

   for (int i = 0; i < page_count; i++) {

      uptr paddr = alloc_pageframe();
      ASSERT(paddr != 0);

      ASSERT(!is_mapped(pdir, (u8 *)vaddr + (i << PAGE_SHIFT)));
      map_page(pdir, (u8 *)vaddr + (i << PAGE_SHIFT), paddr, false, true);
   }

   return true;
}

bool kbasic_virtual_free(uptr vaddr, int page_count)
{
   ASSERT(!(vaddr & (PAGE_SIZE - 1))); // the vaddr must be page-aligned

   page_directory_t *pdir = get_kernel_page_dir();

   if (page_count == 32) {
      uptr paddr = get_mapping(pdir, (void *) vaddr);
      unmap_pages(pdir, (void *)vaddr, 32);
      free_32_pageframes(paddr);
      return true;
   }

   for (int i = 0; i < page_count; i++) {

      void *va = (u8 *)vaddr + (i << PAGE_SHIFT);

      // get_mapping ASSERTs that 'va' is mapped.
      uptr paddr = get_mapping(pdir, va);

      // un-map the virtual address.
      unmap_page(pdir, va);

      // free the physical page as well.
      free_pageframe(paddr);
   }

   return true;
}

