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

   list_node node;
   list_node not_full_heaps_node;
   kmalloc_heap heap;

} small_heap_node;

STATIC kmalloc_small_heaps_stats shs;
STATIC list small_heaps_list;
STATIC list small_not_full_heaps_list;

static inline void
record_small_heap_as_not_full(small_heap_node *node)
{
   list_add_head(&small_not_full_heaps_list, &node->not_full_heaps_node);
   shs.not_full_count++;

   if (shs.not_full_count > shs.peak_not_full_count)
      shs.peak_not_full_count = shs.not_full_count;
}

static inline void
record_small_heap_as_full(small_heap_node *node)
{
   ASSERT(shs.not_full_count > 0);
   shs.not_full_count--;
   list_remove(&node->not_full_heaps_node);
}

static inline void
register_small_heap_node(small_heap_node *new_node)
{
   shs.tot_count++;
   list_add_tail(&small_heaps_list, &new_node->node);

   if (new_node->heap.mem_allocated < new_node->heap.size)
      record_small_heap_as_not_full(new_node);

   if (shs.tot_count > shs.peak_count)
      shs.peak_count = shs.tot_count;
}

static inline void
unregister_small_heap_node(small_heap_node *node)
{
   ASSERT(shs.tot_count > 0);
   shs.tot_count--;
   list_remove(&node->node);

   ASSERT(shs.not_full_count > 0);
   shs.not_full_count--;
   list_remove(&node->not_full_heaps_node);
}

static small_heap_node *alloc_new_small_heap(void)
{
   /*
    * NOTE: here KMALLOC_FL_DONT_ACCOUNT is used in other to avoid the "heavy"
    * kmalloc stats (when enabled) to account this allocation, as it's not
    * really a proper allocation: it's the creation of a small heap. Instead,
    * the allocation for its metadata is explicitly accounted (see below) since
    * some memory was actually consumed.
    */
   size_t actual_metadata_size;
   size_t small_heap_size = SMALL_HEAP_SIZE;
   void *heap_data = general_kmalloc(&small_heap_size, KMALLOC_FL_DONT_ACCOUNT);
   void *metadata = heap_data;
   small_heap_node *new_node;

   if (!heap_data)
      return NULL;

   new_node = kzmalloc(MAX(sizeof(small_heap_node), SMALL_HEAP_MAX_ALLOC + 1));

   if (!new_node) {
      kfree2(heap_data, SMALL_HEAP_SIZE);
      return NULL;
   }

   list_node_init(&new_node->node);
   list_node_init(&new_node->not_full_heaps_node);

   bool success =
      kmalloc_create_heap(&new_node->heap,
                          (uptr)heap_data,
                          SMALL_HEAP_SIZE,
                          SMALL_HEAP_MBS,
                          0,
                          true,
                          metadata,
                          NULL,
                          NULL);

   if (!success) {
      kfree2(new_node, MAX(sizeof(small_heap_node), SMALL_HEAP_MAX_ALLOC + 1));
      kfree2(heap_data, SMALL_HEAP_SIZE);
      return NULL;
   }

   // Allocate heap's metadata inside the heap itself
   actual_metadata_size = new_node->heap.metadata_size;

   DEBUG_ONLY_UNSAFE(void *actual_metadata = )
      per_heap_kmalloc(&new_node->heap, &actual_metadata_size, 0);
   ASSERT(actual_metadata == metadata);

   if (KMALLOC_HEAVY_STATS)
      kmalloc_account_alloc(actual_metadata_size);

   return new_node;
}

static void destroy_small_heap(small_heap_node *node)
{
   kfree2((void *)node->heap.vaddr, SMALL_HEAP_SIZE);
   kfree2(node, MAX(sizeof(small_heap_node), SMALL_HEAP_MAX_ALLOC + 1));
}

static void *small_heaps_kmalloc(size_t *size, u32 flags)
{
   small_heap_node *new_node;
   small_heap_node *pos;
   void *ret;

   ASSERT(!is_preemption_enabled());

   list_for_each_ro(pos, &small_not_full_heaps_list, not_full_heaps_node) {

      if (pos->heap.size - pos->heap.mem_allocated < *size)
         continue;

      ret = per_heap_kmalloc(&pos->heap, size, flags);

      if (!ret)
         continue;

      /* We've found a heap with enough space and the alloc succeeded */

      if (pos->heap.mem_allocated == pos->heap.size)
         record_small_heap_as_full(pos);

      return ret;
   }

   if (!(new_node = alloc_new_small_heap()))
      return NULL;

   // Now finally do the user allocation in the new heap
   ret = per_heap_kmalloc(&new_node->heap, size, flags);

   ASSERT(ret != NULL); // We've just created the node, if should be almost
                        // empty (expect for the metadata). There is no reason
                        // the allocation to fail.

   register_small_heap_node(new_node);
   return ret;
}

static void
small_heaps_kfree(void *ptr, size_t *size, u32 flags)
{
   ASSERT(!is_preemption_enabled());
   small_heap_node *pos, *node = NULL;
   const uptr vaddr = (uptr) ptr;
   bool was_full;

   list_for_each_ro(pos, &small_heaps_list, node) {

      const uptr hva = pos->heap.vaddr;
      const uptr heap_last_byte = pos->heap.heap_last_byte;

      // Check if [vaddr, vaddr + *size - 1] is in [hva, heap_last_byte].
      if (hva <= vaddr && vaddr + *size - 1 <= heap_last_byte) {
         node = pos;
         break;
      }
   }

   if (!node)
      panic("[kfree] Small heap not found for block: %p\n", ptr);

   was_full = node->heap.mem_allocated == node->heap.size;
   per_heap_kfree(&node->heap, ptr, size, flags);

   if (was_full) {
      record_small_heap_as_not_full(node);
   }

   if (node->heap.mem_allocated == SMALL_HEAP_MD_SIZE) {
      unregister_small_heap_node(node);
      destroy_small_heap(node);
   }
}
