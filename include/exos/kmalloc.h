
#pragma once

#include <common/basic_defs.h>
#include <common/string_util.h>

#define KMALLOC_METADATA_BLOCK_NODE_SIZE (1)
#define KMALLOC_HEAPS_COUNT 8

extern bool kmalloc_initialized;

typedef bool (*virtual_alloc_and_map_func)(uptr vaddr, int page_count);
typedef void (*virtual_free_and_unmap_func)(uptr vaddr, int page_count);

typedef struct {

   uptr vaddr;
   size_t size;
   size_t mem_allocated;
   void *metadata_nodes;

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

} kmalloc_heap;

static inline size_t
calculate_heap_metadata_size(size_t heap_size, size_t min_block_size)
{
   return 2 * heap_size / min_block_size;
}

static inline size_t
calculate_heap_min_block_size(size_t heap_size, size_t metadata_size)
{
   return 2 * heap_size / metadata_size;
}

void init_kmalloc();
void *kmalloc(size_t size);
void kfree2(void *ptr, size_t size);
size_t kmalloc_get_total_heap_allocation(void);

bool kmalloc_create_heap(kmalloc_heap *h,
                         uptr vaddr,
                         size_t size,
                         size_t min_block_size,
                         size_t alloc_block_size,
                         bool linear_mapping,
                         void *metadata_nodes,               // optional
                         virtual_alloc_and_map_func valloc,  // optional
                         virtual_free_and_unmap_func vfree); // optional

void kmalloc_destroy_heap(kmalloc_heap *h);
kmalloc_heap *kmalloc_heap_dup(kmalloc_heap *h);

void *internal_kmalloc(kmalloc_heap *h, size_t desired_size);
void internal_kfree2(kmalloc_heap *h, void *ptr, size_t size);

void debug_kmalloc_dump_mem_usage(void);

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
