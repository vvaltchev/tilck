
#pragma once

#include <exos/common/basic_defs.h>
#include <exos/common/string_util.h>

#define KMALLOC_METADATA_BLOCK_NODE_SIZE (1)
#define KMALLOC_HEAPS_COUNT 16

/* Don't touch this. See the comment in kmalloc_heaps.c.h. */
#define KMALLOC_MAX_ALIGN (128 * KB)

extern bool kmalloc_initialized;

typedef bool (*virtual_alloc_and_map_func)(uptr vaddr, int page_count);
typedef void (*virtual_free_and_unmap_func)(uptr vaddr, int page_count);

typedef struct kmalloc_heap kmalloc_heap;

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

size_t kmalloc_get_heap_struct_size(void);

/* kmalloc debug helpers */

void debug_kmalloc_dump_mem_usage(void);
void debug_kmalloc_start_leak_detector(bool save_metadata);
void debug_kmalloc_stop_leak_detector(bool show_leaks);

void debug_kmalloc_start_log(void);
void debug_kmalloc_stop_log(void);
