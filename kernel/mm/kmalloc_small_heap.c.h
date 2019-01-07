/* SPDX-License-Identifier: BSD-2-Clause */

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

   list_node not_full_heaps_node;
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

STATIC int total_small_heaps_count;
STATIC int peak_small_heaps_count;
STATIC int not_full_small_heaps_count;
STATIC int peak_non_full_small_heaps_count;

STATIC list small_not_full_heaps_list;

static inline void
record_small_heap_as_not_full(small_heap_node *node)
{
   list_add_head(&small_not_full_heaps_list, &node->not_full_heaps_node);
   not_full_small_heaps_count++;

   if (not_full_small_heaps_count > peak_non_full_small_heaps_count)
      peak_non_full_small_heaps_count = not_full_small_heaps_count;
}

static inline void
record_small_heap_as_full(small_heap_node *node)
{
   ASSERT(not_full_small_heaps_count > 0);
   not_full_small_heaps_count--;
   list_remove(&node->not_full_heaps_node);
}

static inline void
register_small_heap_node(small_heap_node *new_node)
{
   total_small_heaps_count++;

   if (new_node->heap.mem_allocated < new_node->heap.size)
      record_small_heap_as_not_full(new_node);

   if (total_small_heaps_count > peak_small_heaps_count)
      peak_small_heaps_count = total_small_heaps_count;
}

static inline void
unregister_small_heap_node(small_heap_node *node)
{
   ASSERT(total_small_heaps_count > 0);
   total_small_heaps_count--;

   ASSERT(not_full_small_heaps_count > 0);
   not_full_small_heaps_count--;
   list_remove(&node->not_full_heaps_node);
}

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

   list_node_init(&new_node->not_full_heaps_node);
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
   small_heap_node *pos, *temp;
   void *ret;

   ASSERT(!is_preemption_enabled());

   list_for_each(pos, temp, &small_not_full_heaps_list, not_full_heaps_node) {

      if (pos->heap.size - pos->heap.mem_allocated < *size)
         continue;

      ret = per_heap_kmalloc(&pos->heap, size, flags);

      if (!ret)
         continue;

      /* We've found a heap with enough space and the alloc succeeded */

      if (pos->heap.mem_allocated == pos->heap.size)
         record_small_heap_as_full(pos);

      *chosen_node = pos;
      return ret;
   }

   small_heap_node *new_node = alloc_new_small_heap();

   if (!new_node)
      return NULL;

   // Now finally do the user allocation in the new heap
   ret = per_heap_kmalloc(&new_node->heap, size, flags);

   ASSERT(ret != NULL); // We've just created the node, if should be almost
                        // empty (expect for the metadata). There is no reason
                        // the allocation to fail.

   register_small_heap_node(new_node);
   *chosen_node = new_node;
   return ret;
}

static void destroy_small_heap(small_heap_node *node)
{
   kfree2((void *)node->heap.vaddr, SMALL_HEAP_SIZE);
   kfree2(node, MAX(sizeof(small_heap_node), SMALL_HEAP_MAX_ALLOC + 1));
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
      record_small_heap_as_not_full(md->node);
   }

   if (md->node->heap.mem_allocated == SMALL_HEAP_MD_SIZE) {
      unregister_small_heap_node(md->node);
      destroy_small_heap(md->node);
   }
}
