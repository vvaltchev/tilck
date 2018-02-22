
#include <paging.h>
#include <string_util.h>

/*
 * Allocate and map 32 or 8 physically contiguous pageframes at 'vaddr'.
 * FAILS if such thing is not possible, even if sparse single pageframes are
 * available in the system.
 */
bool kbasic_virtual_alloc(uptr vaddr, int page_count)
{
   ASSERT(!(vaddr & (PAGE_SIZE - 1))); // the vaddr must be page-aligned
   ASSERT(page_count == 32 || page_count == 8 || page_count == 1);

   if (get_free_pageframes_count() < page_count)
      return false;

   page_directory_t *pdir = get_kernel_page_dir();

   uptr paddr;

   switch (page_count) {
   case 32:
      paddr = alloc_32_pageframes();
      break;
   case 8:
      paddr = alloc_8_pageframes();
      break;
   case 1:
      paddr = alloc_pageframe();
      break;
   default:
      NOT_REACHED();
   }

   if (paddr != INVALID_PADDR) {
      map_pages(pdir, (void *)vaddr, paddr, page_count, false, true);
      return true;
   }

   if (page_count != 32)
      return false;

   // We failed to allocate 32 physically contiguous pages.
   // Let's try to allocate them in groups of 8.

   uptr paddrs[4] = {0};

   for (int i = 0; i < 4; i++) {

      paddr = alloc_8_pageframes();

      if (paddr == INVALID_PADDR) {
         // Oops, we failed here too. Rollback.
         for (i--; i >= 0; i--)
            free_8_pageframes(paddrs[i]);
         return false;
      }

      paddrs[i] = paddr;
   }


   for (int i = 0; i < 4; i++) {
      map_pages(pdir,
                (void *) (vaddr + i * 8 * PAGE_SIZE),
                paddrs[i],
                8, false, true);
   }

   return true;
}

/*
 * Un-maps and frees a block of 32 or 8 pageframes mapped at 'vaddr'.
 */
bool kbasic_virtual_free(uptr vaddr, int page_count)
{
   ASSERT(!(vaddr & (PAGE_SIZE - 1))); // the vaddr must be page-aligned
   ASSERT(page_count == 32 || page_count == 8 || page_count == 1);

   page_directory_t *pdir = get_kernel_page_dir();

   if (page_count == 1) {
      free_pageframe(get_mapping(pdir, (void *)vaddr));
      goto end;
   }

   free_8_pageframes(get_mapping(pdir, (void *) vaddr));

   if (page_count == 32) {
      free_8_pageframes(get_mapping(pdir, (void *) (vaddr + 8 * PAGE_SIZE)));
      free_8_pageframes(get_mapping(pdir, (void *) (vaddr + 16 * PAGE_SIZE)));
      free_8_pageframes(get_mapping(pdir, (void *) (vaddr + 24 * PAGE_SIZE)));
   }

   end:
   unmap_pages(pdir, (void *)vaddr, page_count);
   return true;
}

