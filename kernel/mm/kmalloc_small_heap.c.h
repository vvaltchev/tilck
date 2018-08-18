
#ifndef _KMALLOC_C_

   #error This is NOT a header file and it is not meant to be included

#endif

#define SMALL_HEAP_MBS  16
#define SMALL_HEAP_SIZE (8 * PAGE_SIZE)

#define SMALL_HEAP_MD_SIZE \
   (calculate_heap_metadata_size(SMALL_HEAP_SIZE, SMALL_HEAP_MBS))

/*
 * NOTE: the trick to make the small heap to work well without the number of
 * small heaps to explode is to allow it to allocate just a small fraction of
 * its actual size, like 1/16th.
 */
#define SMALL_HEAP_MAX_ALLOC (SMALL_HEAP_SIZE / 16 - 1)


typedef struct {

   list_node not_full_heaps_list;
   list_node list;

   kmalloc_heap heap;

} small_heap_node;

typedef struct {

   small_heap_node *node;
   u16 size;
   u16 align_offset;

#ifdef BITS64
   u32 padding;
#endif

} small_heap_block_metadata;

STATIC_ASSERT(sizeof(small_heap_block_metadata) == 2 * sizeof(uptr));

STATIC list_node small_heaps_list;
STATIC list_node small_not_full_heaps_list;

static const u32 align_type_table[4] =
{
    2 * sizeof(uptr),
    4 * sizeof(uptr),
    8 * sizeof(uptr),
   16 * sizeof(uptr)
};

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

static void *small_heap_kmalloc(size_t size, u32 flags)
{
   void *buf;
   small_heap_node *node;
   u32 align_offset = 0;
   u32 align = align_type_table[(flags & KMALLOC_FL_ALIGN_TYPE_MASK) >> 28];

   ASSERT(size <= SMALL_HEAP_MAX_ALLOC);

   if (align > sizeof(small_heap_block_metadata)) {
      align_offset = align - sizeof(small_heap_block_metadata);
   }

   size += sizeof(small_heap_block_metadata) + align_offset;
   buf = small_heap_kmalloc_internal(&size, flags, &node);

   if (!buf)
      return NULL;

   small_heap_block_metadata *md = buf + align_offset;
   md->node = node;
   md->size = size;
   md->align_offset = align_offset;
   return md + 1;
}

static void
small_heap_kfree(void *ptr, u32 flags)
{
   ASSERT(!is_preemption_enabled());

   small_heap_block_metadata *md = (small_heap_block_metadata *)ptr - 1;
   bool was_full = md->node->heap.mem_allocated == md->node->heap.size;
   size_t actual_size = md->size;
   void *block_ptr = (char *)md - md->align_offset;

   per_heap_kfree(&md->node->heap, block_ptr, &actual_size, flags);
   ASSERT(actual_size == md->size);

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
