
#pragma once

#define STACK_VAR (h->alloc_stack)
#include <tilck/common/norec.h>

#define KMALLOC_ALLOC_STACK_SIZE 32

struct kmalloc_heap {

   uptr vaddr;
   size_t size;
   size_t mem_allocated;
   void *metadata_nodes;
   int region;

   size_t min_block_size;
   size_t alloc_block_size;

   virtual_alloc_and_map_func valloc_and_map;
   virtual_free_and_unmap_func vfree_and_unmap;

   /* -- pre-calculated values -- */
   size_t heap_data_size_log2;
   size_t alloc_block_size_log2;
   size_t metadata_size;
   uptr heap_over_end; /* addr + size == last_heap_byte + 1 */
   /* -- */

   bool linear_mapping;

   /*
    * Explicit stack used by per_heap_kmalloc()
    *
    * NOTE: this stack is per-heap because some kmalloc heaps might use
    * kmalloc itself as valloc/vfree funcs [see process_mm.c]. In such cases,
    * is unsafe to keep just one global alloc_stack.
    */
   struct explicit_stack_elem2 alloc_stack[KMALLOC_ALLOC_STACK_SIZE];
};


void
internal_kmalloc_split_block(kmalloc_heap *h,
                             void *const vaddr,
                             const size_t block_size,
                             const size_t leaf_node_size);

size_t
internal_kmalloc_coalesce_block(kmalloc_heap *h,
                                void *const vaddr,
                                const size_t block_size);
