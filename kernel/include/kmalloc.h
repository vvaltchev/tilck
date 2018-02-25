
#pragma once

#include <common_defs.h>
#include <string_util.h>

#define KMALLOC_METADATA_BLOCK_NODE_SIZE (1)

typedef struct {

   uptr addr;
   size_t size;
   void *metadata_nodes;

   size_t min_block_size;
   size_t alloc_block_size;
   size_t heap_data_size_log2;
   size_t alloc_block_size_log2;

} kmalloc_heap;

static inline size_t calculate_heap_metadata_size(size_t heap_size,
                                                  size_t min_block_size)
{
   return ((2 * heap_size) / min_block_size) * KMALLOC_METADATA_BLOCK_NODE_SIZE;
}


void initialize_kmalloc();
void *kmalloc(size_t size);
void kfree(void *ptr, size_t size);

static inline void *kzmalloc(size_t size)
{
   void *res = kmalloc(size);

   if (!res)
      return NULL;

   bzero(res, size);
   return res;
}
