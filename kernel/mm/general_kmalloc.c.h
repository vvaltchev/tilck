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
__general_kmalloc(size_t *size, u32 flags)
{
   ASSERT(kmalloc_initialized);

   if (*size <= SMALL_HEAP_MAX_ALLOC) {
      return small_heap_kmalloc(*size, flags);
   }

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
__general_kfree(void *ptr, size_t *size, u32 flags)
{
   kmalloc_heap *h = NULL;
   const uptr vaddr = (uptr) ptr;
   ASSERT(kmalloc_initialized);

   if (!ptr)
      return;

   if (*size <= SMALL_HEAP_MAX_ALLOC) {
      return small_heap_kfree(ptr, flags);
   }

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
   ASSERT(size != NULL);
   ASSERT(*size);

   disable_preemption();
   {
      const size_t orig_size = *size;

      res = __general_kmalloc(size, flags);

      if (KMALLOC_HEAVY_STATS && res != NULL)
         kmalloc_account_alloc(orig_size);
   }
   enable_preemption();
   return res;
}

void general_kfree(void *ptr, size_t *size, u32 flags)
{
   ASSERT(size != NULL);
   ASSERT(!ptr || *size);

   disable_preemption();
   {
      __general_kfree(ptr, size, flags);
   }
   enable_preemption();
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
      .buf = NULL,
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
