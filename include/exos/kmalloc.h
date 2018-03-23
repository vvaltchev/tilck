
#pragma once

#include <common/common_defs.h>
#include <common/string_util.h>

#define KMALLOC_METADATA_BLOCK_NODE_SIZE (1)
#define KMALLOC_HEAPS_COUNT 8

typedef struct {

   uptr vaddr;
   size_t size;
   size_t mem_allocated;
   void *metadata_nodes;

   size_t min_block_size;
   size_t alloc_block_size;

   /* -- pre-calculated values -- */
   size_t heap_data_size_log2;
   size_t alloc_block_size_log2;
   size_t metadata_size;
   uptr heap_over_end; /* addr + size == last_heap_byte + 1 */
   /* -- */

   bool linear_mapping;

} kmalloc_heap;

static inline size_t calculate_heap_metadata_size(size_t heap_size,
                                                  size_t min_block_size)
{
   return ((2 * heap_size) / min_block_size) * KMALLOC_METADATA_BLOCK_NODE_SIZE;
}


void initialize_kmalloc();
void *kmalloc(size_t size);
void kfree2(void *ptr, size_t size);

static ALWAYS_INLINE void kfree(void *ptr)
{
   kfree2(ptr, 0);
}

static inline void *kzmalloc(size_t size)
{
   void *res = kmalloc(size);

   if (!res)
      return NULL;

   bzero(res, size);
   return res;
}
