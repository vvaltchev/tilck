/* SPDX-License-Identifier: BSD-2-Clause */

#ifndef _KMALLOC_C_

   #error This is NOT a header file and it is not meant to be included

   /*
    * The only purpose of this file is to keep kmalloc.c shorter.
    * Yes, this file could be turned into a regular C source file, but at the
    * price of making several static functions and variables in kmalloc.c to be
    * just non-static. We don't want that. Code isolation is a GOOD thing.
    */

#endif

static void *
main_heaps_kmalloc(size_t *size, u32 flags)
{
   ASSERT(kmalloc_initialized);

   // Iterate in reverse-order because the first heaps are the biggest ones.
   for (int i = used_heaps - 1; i >= 0; i--) {

      ASSERT(heaps[i] != NULL);

      void *vaddr;
      const size_t heap_size = heaps[i]->size;
      const size_t heap_free = heap_size - heaps[i]->mem_allocated;

      /*
       * The heap is too small (unlikely but possible) or the heap has not been
       * created yet, therefore has size = 0 or just there is not enough free
       * space in it.
       */
      if (heap_size < *size || heap_free < *size)
         continue;

      if (heaps[i]->dma != !!(flags & KMALLOC_FL_DMA))
         continue;

      if ((vaddr = per_heap_kmalloc(heaps[i], size, flags))) {

         if (KMALLOC_SUPPORT_LEAK_DETECTOR && leak_detector_enabled) {
            debug_kmalloc_register_alloc(vaddr, *size);
         }

         return vaddr;
      }
   }

   return NULL;
}

static int
main_heaps_kfree(void *ptr, size_t *size, u32 flags)
{
   struct kmalloc_heap *h = NULL;
   const ulong vaddr = (ulong) ptr;
   ASSERT(kmalloc_initialized);

   for (int i = used_heaps - 1; i >= 0; i--) {

      const ulong hva = heaps[i]->vaddr;
      const ulong hend = heaps[i]->heap_last_byte-heaps[i]->min_block_size+1;

      if (IN_RANGE_INC(vaddr, hva, hend)) {
         h = heaps[i];
         break;
      }
   }

   if (!h)
      return -ENOENT;

   /*
    * Vaddr must be aligned at least at min_block_size otherwise, something is
    * wrong with it, maybe it has been allocated with mdalloc()?
    */
   ASSERT((vaddr & (h->min_block_size - 1)) == 0);

   per_heap_kfree(h, ptr, size, flags);

   if (KMALLOC_FREE_MEM_POISONING) {
      memset32(ptr, FREE_MEM_POISON_VAL, *size / 4);
   }

   if (KMALLOC_SUPPORT_LEAK_DETECTOR && leak_detector_enabled) {
      debug_kmalloc_register_free((void *)vaddr, *size);
   }

   return 0;
}

void *general_kmalloc(size_t *size, u32 flags)
{
   void *res;
   const u32 sub_block_sz = flags & KMALLOC_FL_SUB_BLOCK_MIN_SIZE_MASK;
   ASSERT(kmalloc_initialized);
   ASSERT(size != NULL);
   ASSERT(*size);

   disable_preemption();
   {
      const size_t orig_size = *size;

      if (*size <= SMALL_HEAP_MAX_ALLOC ||
          UNLIKELY(sub_block_sz && sub_block_sz <= SMALL_HEAP_MAX_ALLOC))
      {
         /* Small DMA allocations are not allowed */
         ASSERT(~flags & KMALLOC_FL_DMA);
         res = small_heaps_kmalloc(size, flags);

      } else {

         res = main_heaps_kmalloc(size, flags);

         if (UNLIKELY(res == NULL && ~flags & KMALLOC_FL_DMA))
            res = main_heaps_kmalloc(size, flags | KMALLOC_FL_DMA);
      }

      if (KMALLOC_HEAVY_STATS && res != NULL)
         if (~flags & KMALLOC_FL_DONT_ACCOUNT)
            kmalloc_account_alloc(orig_size);
   }
   enable_preemption();
   return res;
}

void general_kfree(void *ptr, size_t *size, u32 flags)
{
   int rc;

   ASSERT(kmalloc_initialized);
   ASSERT(size != NULL);

   if (!ptr)
      return;

   if (flags & (KFREE_FL_MULTI_STEP | KFREE_FL_ALLOW_SPLIT)) {
      /* For these special cases, the `size` parameter is MANDATORY */
      ASSERT(*size);
   }

   disable_preemption();
   {
      if (*size) {

         /* We know which heap set contains our chunk */

         if (*size <= SMALL_HEAP_MAX_ALLOC) {
            rc = small_heaps_kfree(ptr, size, flags);
         } else {
            rc = main_heaps_kfree(ptr, size, flags);
         }


      } else {

         /* We don't know which heap set contains our chunk: try them both */

         rc = small_heaps_kfree(ptr, size, flags);

         if (rc)
            rc = main_heaps_kfree(ptr, size, flags);
      }
   }
   enable_preemption();

   if (rc)
      panic("kfree: Heap not found for block: %p\n", ptr);
}

/*
 * Currently, because of the way kmalloc() is implemented, each chunk of actual
 * size roundup_next_power_of_2(size) with size < KMALLOC_MAX_ALIGN, is
 * automatically aligned without the need of any effort. Still, it's worth
 * using functions like aligned_kmalloc() and aligned_kfree() in order to save
 * the alignment requirement and allow potentially the use of a different
 * allocator.
 */
void *aligned_kmalloc(size_t size, u32 align)
{
   void *res = general_kmalloc(&size, 0);

   ASSERT(align > 0);
   ASSERT(align <= size);
   ASSERT(align <= KMALLOC_MAX_ALIGN);
   ASSERT(roundup_next_power_of_2(align) == align);

   if (res != NULL)
      ASSERT(((ulong)res & (align-1)) == 0);

   return res;
}

/* See the comment above aligned_kmalloc() */
void aligned_kfree2(void *ptr, size_t size)
{
   general_kfree(ptr, &size, 0);
}
