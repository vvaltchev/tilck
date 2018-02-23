
#include <paging.h>
#include <string_util.h>



// bool kbasic_virtual_free(uptr vaddr, int page_count)
// {
//    ASSERT(!(vaddr & (PAGE_SIZE - 1))); // the vaddr must be page-aligned

//    page_directory_t *pdir = get_kernel_page_dir();

//    // const int page_count_div_8 = page_count >> 3;

//    // for (int i = 0; i < page_count_div_8; i++) {
//    //    free_8_pageframes(get_mapping(pdir, (void *)vaddr));
//    //    vaddr += 8 * PAGE_SIZE;
//    // }

//    // const int rem = page_count - page_count_div_8;

//    for (int i = 0; i < page_count; i++) {
//       free_pageframe(get_mapping(pdir, (void *)vaddr));
//       vaddr += PAGE_SIZE;
//    }

//    unmap_pages(pdir, (void *)vaddr, page_count);
//    return true;
// }

bool kbasic_virtual_alloc(uptr vaddr, int pageCount)
{
   ASSERT(!(vaddr & (PAGE_SIZE - 1))); // the vaddr must be page-aligned

   page_directory_t *pdir = get_kernel_page_dir();

   // Ensure that we have enough physical memory.
   if (get_free_pageframes_count() < pageCount) {
      return false;
   }

   for (int i = 0; i < pageCount; i++) {

      uptr paddr = alloc_pageframe();
      ASSERT(paddr != INVALID_PADDR);
      map_page(pdir, (u8 *)vaddr + (i << PAGE_SHIFT), paddr, false, true);
   }

   return true;
}



bool kbasic_virtual_free(uptr vaddr, int pageCount)
{
   ASSERT(!(vaddr & (PAGE_SIZE - 1))); // the vaddr must be page-aligned

   page_directory_t *pdir = get_kernel_page_dir();

   for (int i = 0; i < pageCount; i++) {

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

// bool kbasic_virtual_alloc(uptr vaddr, int page_count)
// {
//    ASSERT(!(vaddr & (PAGE_SIZE - 1))); // the vaddr must be page-aligned

//    if (get_free_pageframes_count() < page_count)
//       return false;

//    page_directory_t *pdir = get_kernel_page_dir();

//    int allocated = 0;
//    uptr curr_vaddr = vaddr;

//    // const int page_count_div_32 = page_count >> 5;

//    // for (int i = 0; i < page_count_div_32; i++) {

//    //    uptr paddr = alloc_32_pageframes();

//    //    if (paddr == INVALID_PADDR)
//    //       break;

//    //    map_pages(pdir, (void *)curr_vaddr, paddr, 32, false, true);

//    //    curr_vaddr += 32 * PAGE_SIZE;
//    //    allocated += 32;
//    // }

//    const int rem_div_8 = (page_count - allocated) >> 3;

//    for (int i = 0; i < rem_div_8; i++) {

//       uptr paddr = alloc_8_pageframes();

//       if (paddr == INVALID_PADDR) {

//          // Oops, we were unable to allocate 8 contiguous page frames.
//          // We cannot allocate them one by one, since the free function
//          // will free them in blocks of 8.
//          NOT_REACHED();
//          bool res = kbasic_virtual_free(vaddr, allocated);
//          VERIFY(res);
//          return false;
//       }

//       map_pages(pdir, (void *)curr_vaddr, paddr, 8, false, true);
//       curr_vaddr += 8 * PAGE_SIZE;
//       allocated += 8;
//    }

//    const int rem = page_count - allocated;
//    ASSERT(rem == 0);

//    for (int i = 0; i < rem; i++) {

//       uptr paddr = alloc_pageframe();

//       // We cannot fail here since we checked at the beginning that we have
//       // enough free pageframes.
//       VERIFY(paddr != INVALID_PADDR);

//       map_pages(pdir, (void *)curr_vaddr, paddr, 1, false, true);
//       curr_vaddr += PAGE_SIZE;
//       allocated++;
//    }

//    ASSERT(allocated == page_count);
//    return true;
// }

