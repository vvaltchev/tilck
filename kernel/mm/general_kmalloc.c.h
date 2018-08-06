
#ifndef _KMALLOC_C_

   #error This is NOT a header file and it is not meant to be included

   /*
    * The only purpose of this file is to keep kmalloc.c shorter.
    * Yes, this file could be turned into a regular C source file, but at the
    * price of making several static functions and variables in kmalloc.c to be
    * just non-static. We don't want that. Code isolation is a GOOD thing.
    */

#endif


void *
general_kmalloc(size_t *size,
                bool multi_step_alloc,
                size_t sub_blocks_min_size)
{
   void *ret = NULL;

   ASSERT(kmalloc_initialized);
   disable_preemption();

   // Iterate in reverse-order because the first heaps are the biggest ones.
   for (int i = used_heaps - 1; i >= 0; i--) {

      ASSERT(heaps[i] != NULL);

      const size_t heap_size = heaps[i]->size;
      const size_t heap_free = heap_size - heaps[i]->mem_allocated;

      /*
       * The heap is too small (unlikely but possible) or the heap has not been
       * created yet, therefore has size = 0 or just there is not enough free
       * space in it.
       */
      if (heap_size < *size || heap_free < *size)
         continue;

      void *vaddr = per_heap_kmalloc(heaps[i], size, false, 0);

      if (vaddr) {
         heaps[i]->mem_allocated += *size;
         ret = vaddr;

         if (KMALLOC_SUPPORT_LEAK_DETECTOR && leak_detector_enabled) {
            debug_kmalloc_register_alloc(vaddr, *size);
         }

         break;
      }
   }

   enable_preemption();
   return ret;
}

void
general_kfree(void *ptr,
              size_t *size,
              bool allow_split,
              bool multi_step_free)
{
   const uptr vaddr = (uptr) ptr;

   ASSERT(kmalloc_initialized);

   if (!ptr)
      return;

   disable_preemption();

   int hn = -1; /* the heap with the highest vaddr <= our block vaddr */

   for (int i = used_heaps - 1; i >= 0; i--) {

      uptr hva = heaps[i]->vaddr;

      if (vaddr < hva)
         continue; /* not in this heap, for sure */

      if (hn < 0 || hva > heaps[hn]->vaddr)
         hn = i;
   }

   if (hn < 0)
      goto out; /* no need to re-enable the preemption, we're going to panic */

   per_heap_kfree(heaps[hn], ptr, size, false, false);
   heaps[hn]->mem_allocated -= *size;

   if (KMALLOC_FREE_MEM_POISONING) {
      memset32(ptr, KMALLOC_FREE_MEM_POISON_VAL, *size / 4);
   }

   if (KMALLOC_SUPPORT_LEAK_DETECTOR && leak_detector_enabled) {
      debug_kmalloc_register_free((void *)vaddr, *size);
   }

   enable_preemption();
   return;

out:
   panic("[kfree] Heap not found for block: %p\n", ptr);
}
