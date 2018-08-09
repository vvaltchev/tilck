
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

      void *vaddr = per_heap_kmalloc(heaps[i],
                                     size,
                                     multi_step_alloc,
                                     sub_blocks_min_size);

      if (vaddr) {
         ret = vaddr;

         if (KMALLOC_SUPPORT_LEAK_DETECTOR && leak_detector_enabled) {
            debug_kmalloc_register_alloc(vaddr, *size);
         }

         break;
      }
   }

out:
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
      panic("[kfree] Heap not found for block: %p\n", ptr);

   per_heap_kfree(heaps[hn], ptr, size, allow_split, multi_step_free);

   if (KMALLOC_FREE_MEM_POISONING) {
      memset32(ptr, KMALLOC_FREE_MEM_POISON_VAL, *size / 4);
   }

   if (KMALLOC_SUPPORT_LEAK_DETECTOR && leak_detector_enabled) {
      debug_kmalloc_register_free((void *)vaddr, *size);
   }

out:
   enable_preemption();
   return;
}

void
kmalloc_create_accelerator(kmalloc_accelerator *a, u32 elem_size, u32 elem_c)
{
   /* The both elem_size and elem_count must be a power of 2 */
   ASSERT(roundup_next_power_of_2(elem_size) == elem_size);
   ASSERT(roundup_next_power_of_2(elem_c) == elem_c);

   *a = (kmalloc_accelerator) {
      .elem_size = elem_size,
      .elem_count = elem_c,
      .curr_elem = elem_c,
      .buf = NULL
   };
}

void *
kmalloc_accelerator_get_elem(kmalloc_accelerator *a)
{
   size_t actual_size;

   if (a->curr_elem == a->elem_count) {

      actual_size = a->elem_size * a->elem_count;

      a->buf = general_kmalloc(&actual_size,   /* size (in/out)       */
                               false,          /* multi-step alloc    */
                               a->elem_size);  /* sub_blocks_min_size */

      ASSERT(actual_size == a->elem_size * a->elem_count);

      if (!a->buf)
         return NULL;

      a->curr_elem = 0;
   }

   return a->buf + (a->elem_size * a->curr_elem++);
}

void
kmalloc_destroy_accelerator(kmalloc_accelerator *a)
{
   size_t actual_size;

   for (; a->curr_elem < a->elem_count; a->curr_elem++) {

      actual_size = a->elem_size;

      general_kfree(a->buf + (a->curr_elem * a->elem_size), /* ptr           */
                    &actual_size,                           /* size (in/out) */
                    true,                                   /* allow_split   */
                    false);                                 /* multi_step    */

      ASSERT(actual_size == a->elem_size);
   }
}
