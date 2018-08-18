
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

void *
general_kmalloc(size_t *size, u32 flags)
{
   void *ret = NULL;

   ASSERT(kmalloc_initialized);
   ASSERT(size);
   ASSERT(*size);

   disable_preemption();

   if (*size <= SMALL_HEAP_MAX_ALLOC) {
      ret = small_heap_kmalloc(*size, flags);
      goto out;
   }

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

      void *vaddr = per_heap_kmalloc(heaps[i], size, flags);

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
general_kfree(void *ptr, size_t *size, u32 flags)
{
   const uptr vaddr = (uptr) ptr;
   ASSERT(kmalloc_initialized);

   if (!ptr)
      return;

   ASSERT(*size);
   disable_preemption();

   if (*size <= SMALL_HEAP_MAX_ALLOC) {
      small_heap_kfree(ptr, flags);
      goto out;
   }

   kmalloc_heap *h = NULL;

   for (int i = used_heaps - 1; i >= 0; i--) {

      uptr hva = heaps[i]->vaddr;

      if (hva <= vaddr && vaddr + *size <= heaps[i]->heap_over_end) {
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

   /* Max elem_size: 512 MB */
   ASSERT(elem_size <= (1 << 29));

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
                    KFREE_FL_ALLOW_SPLIT);

      ASSERT(actual_size == a->elem_size);
   }
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
