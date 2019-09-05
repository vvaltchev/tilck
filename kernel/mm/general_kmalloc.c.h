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

#define MDALLOC_MAGIC (0x28D119D8D488EFB5ull)

/*
 * Metadata block prepending the actual data used by mdalloc() and mdfree().
 */
typedef struct {

#ifdef DEBUG
   u64 magic;
#endif

   uptr size;

} mdalloc_metadata;

static void *
main_heaps_kmalloc(size_t *size, u32 flags)
{
   ASSERT(kmalloc_initialized);

   // Iterate in reverse-order because the first heaps are the biggest ones.
   for (int i = (int)used_heaps - 1; i >= 0; i--) {

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

      if ((vaddr = per_heap_kmalloc(heaps[i], size, flags))) {

         if (KMALLOC_SUPPORT_LEAK_DETECTOR && leak_detector_enabled) {
            debug_kmalloc_register_alloc(vaddr, *size);
         }

         return vaddr;
      }
   }

   return NULL;
}

static void
main_heaps_kfree(void *ptr, size_t *size, u32 flags)
{
   kmalloc_heap *h = NULL;
   const uptr vaddr = (uptr) ptr;
   ASSERT(kmalloc_initialized);

   for (int i = (int)used_heaps - 1; i >= 0; i--) {

      uptr hva = heaps[i]->vaddr;

      // Check if [vaddr, vaddr + *size - 1] is in [hva, heap_last_byte].
      if (hva <= vaddr && vaddr + *size - 1 <= heaps[i]->heap_last_byte) {
         h = heaps[i];
         break;
      }
   }

   if (!h)
      panic("[kfree] Heap not found for block: %p\n", ptr);

   /*
    * Vaddr must be aligned at least at min_block_size otherwise, something is
    * wrong with it, maybe it has been allocated with mdalloc()?
    */
   ASSERT((vaddr & (h->min_block_size - 1)) == 0);

   per_heap_kfree(h, ptr, size, flags);

   if (KMALLOC_FREE_MEM_POISONING) {
      memset32(ptr, KMALLOC_FREE_MEM_POISON_VAL, *size / 4);
   }

   if (KMALLOC_SUPPORT_LEAK_DETECTOR && leak_detector_enabled) {
      debug_kmalloc_register_free((void *)vaddr, *size);
   }
}

void *general_kmalloc(size_t *size, u32 flags)
{
   void *res;
   ASSERT(kmalloc_initialized);
   ASSERT(size != NULL);
   ASSERT(*size);

   disable_preemption();
   {
      const size_t orig_size = *size;

      if (*size <= SMALL_HEAP_MAX_ALLOC) {
         res = small_heaps_kmalloc(*size, flags);
      } else {
         res = main_heaps_kmalloc(size, flags);
      }

      if (KMALLOC_HEAVY_STATS && res != NULL)
         if (!(flags & KMALLOC_FL_DONT_ACCOUNT))
            kmalloc_account_alloc(orig_size);
   }
   enable_preemption();
   return res;
}

void general_kfree(void *ptr, size_t *size, u32 flags)
{
   ASSERT(kmalloc_initialized);
   ASSERT(size != NULL);
   ASSERT(!ptr || *size);

   if (!ptr)
      return;

   disable_preemption();
   {
      if (*size <= SMALL_HEAP_MAX_ALLOC) {
         small_heaps_kfree(ptr, flags);
      } else {
         main_heaps_kfree(ptr, size, flags);
      }
   }
   enable_preemption();
}

void *mdalloc(size_t size)
{
   mdalloc_metadata *b = kmalloc(size + sizeof(mdalloc_metadata));

   if (!b)
      return NULL;

#ifdef DEBUG
   b->magic = MDALLOC_MAGIC;
#endif

   b->size = size;
   return b + 1;
}

void mdfree(void *ptr)
{
   if (!ptr)
      return;

   mdalloc_metadata *b = ptr - sizeof(mdalloc_metadata);
   ASSERT(b->magic == MDALLOC_MAGIC);

   kfree2(b, b->size + sizeof(mdalloc_metadata));
}
