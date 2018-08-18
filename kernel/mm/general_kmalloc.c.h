
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

typedef struct {

   list_node not_full_heaps_list;
   list_node list;

   kmalloc_heap heap;

} small_heap_node;

typedef struct {

   small_heap_node *node;
   uptr size;
   uptr padding[2]; // TODO: could we avoid having permanently this padding?

} small_heap_block_metadata;

#define SMALL_HEAP_MBS 32

#define SMALL_HEAP_MD_SIZE \
   (calculate_heap_metadata_size(SMALL_HEAP_SIZE, SMALL_HEAP_MBS))

static small_heap_node *alloc_new_small_heap(void)
{
   void *heap_data = kmalloc(SMALL_HEAP_SIZE);

   if (!heap_data)
      return NULL;

   small_heap_node *new_node =
      kzmalloc(MAX(sizeof(small_heap_node), SMALL_HEAP_MAX_ALLOC + 1));

   if (!new_node) {
      kfree2(heap_data, SMALL_HEAP_SIZE);
      return NULL;
   }

   list_node_init(&new_node->not_full_heaps_list);
   list_node_init(&new_node->list);
   void *md_alloc = heap_data;

   bool success =
      kmalloc_create_heap(&new_node->heap,
                          (uptr)heap_data,
                          SMALL_HEAP_SIZE,
                          SMALL_HEAP_MBS,
                          0,
                          true,
                          md_alloc,
                          NULL,
                          NULL);

   if (!success) {
      kfree2(new_node, MAX(sizeof(small_heap_node), SMALL_HEAP_MAX_ALLOC + 1));
      kfree2(heap_data, SMALL_HEAP_SIZE);
      return NULL;
   }

   size_t actual_size;

   // Allocate heap's metadata inside the heap itself
   actual_size = new_node->heap.metadata_size;

   DEBUG_ONLY(void *actual_md_alloc = )
      per_heap_kmalloc(&new_node->heap, &actual_size, 0);
   ASSERT(actual_md_alloc == md_alloc);

   return new_node;
}

static void *
small_heap_kmalloc_internal(size_t *size,
                            u32 flags,
                            small_heap_node **chosen_node)
{
   small_heap_node *pos;
   ASSERT(!is_preemption_enabled());

   list_for_each(pos, &small_not_full_heaps_list, not_full_heaps_list) {

      const size_t heap_free_mem = pos->heap.size - pos->heap.mem_allocated;

      if (heap_free_mem >= *size) {

         void *ret = per_heap_kmalloc(&pos->heap, size, flags);

         if (ret) {

            if (pos->heap.mem_allocated == pos->heap.size)
               list_remove(&pos->not_full_heaps_list);

            *chosen_node = pos;
            return ret;
         }
      }
   }

   small_heap_node *new_node = alloc_new_small_heap();

   if (!new_node)
      return NULL;

   // Now finally do the user allocation in the new heap
   void *ret = per_heap_kmalloc(&new_node->heap, size, flags);
   ASSERT(ret != NULL);

   list_add_before(&small_heaps_list, &new_node->list);

   if (new_node->heap.mem_allocated < new_node->heap.size)
      list_add_before(&small_not_full_heaps_list, &new_node->not_full_heaps_list);

   *chosen_node = new_node;
   return ret;
}

static void *small_heap_kmalloc(size_t *size, u32 flags)
{
   void *buf;
   small_heap_node *node;

   *size += sizeof(small_heap_block_metadata);
   buf = small_heap_kmalloc_internal(size, flags, &node);

   if (!buf)
      return NULL;

   small_heap_block_metadata *md = buf;
   md->node = node;
   md->size = *size;
   return md + 1;
}

static void
small_heap_kfree(void *ptr, size_t *size, u32 flags)
{
   ASSERT(!is_preemption_enabled());

   small_heap_block_metadata *md = (small_heap_block_metadata *)ptr - 1;

   bool was_full = md->node->heap.mem_allocated == md->node->heap.size;

   *size += sizeof(small_heap_block_metadata);
   per_heap_kfree(&md->node->heap, md, size, flags);

   if (was_full) {
      list_add_tail(&small_not_full_heaps_list,
                    &md->node->not_full_heaps_list);
   }

   if (md->node->heap.mem_allocated == SMALL_HEAP_MD_SIZE) {
      list_remove(&md->node->not_full_heaps_list);
      list_remove(&md->node->list);
      kfree2((void *)md->node->heap.vaddr, SMALL_HEAP_SIZE);
      kfree2(md->node, MAX(sizeof(small_heap_node), SMALL_HEAP_MAX_ALLOC + 1));
   }
}

void *
general_kmalloc(size_t *size, u32 flags)
{
   void *ret = NULL;

   ASSERT(kmalloc_initialized);
   ASSERT(size);
   ASSERT(*size);

   disable_preemption();

   if (*size <= SMALL_HEAP_MAX_ALLOC) {
      ret = small_heap_kmalloc(size, flags);
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
      small_heap_kfree(ptr, size, flags);
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
