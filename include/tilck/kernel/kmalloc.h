
#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#define KMALLOC_METADATA_BLOCK_NODE_SIZE (1)
#define KMALLOC_HEAPS_COUNT 32

#define KMALLOC_FL_MULTI_STEP               ((u32)1 << 31)
#define KMALLOC_FL_RES1                     ((u32)1 << 30)
#define KMALLOC_FL_SUB_BLOCK_MIN_SIZE_MASK  (((u32)1 << 30) - 1)

#define KFREE_FL_MULTI_STEP                 ((u32)1 << 31)
#define KFREE_FL_ALLOW_SPLIT                ((u32)1 << 30)

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

void init_kmalloc(void);

void *general_kmalloc(size_t *size, u32 flags);
void general_kfree(void *ptr, size_t *size, u32 flags);

size_t kmalloc_get_total_heap_allocation(void);
bool is_kmalloc_initialized(void);

bool kmalloc_create_heap(kmalloc_heap *h,
                         uptr vaddr,
                         size_t size,
                         size_t min_block_size,
                         size_t alloc_block_size, /* 0 if linear_mapping=1 */
                         bool linear_mapping,
                         void *metadata_nodes,               // optional
                         virtual_alloc_and_map_func valloc,  // optional
                         virtual_free_and_unmap_func vfree); // optional

void kmalloc_destroy_heap(kmalloc_heap *h);
kmalloc_heap *kmalloc_heap_dup(kmalloc_heap *h);

void *per_heap_kmalloc(kmalloc_heap *h, size_t *size, u32 flags);
void per_heap_kfree(kmalloc_heap *h, void *ptr, size_t *size, u32 flags);

typedef struct {

   u32 elem_size;
   u32 elem_count;
   u32 curr_elem;
   char *buf;

} kmalloc_accelerator;

void kmalloc_create_accelerator(kmalloc_accelerator *a, u32 elem_s, u32 elem_c);
void *kmalloc_accelerator_get_elem(kmalloc_accelerator *a);
void kmalloc_destroy_accelerator(kmalloc_accelerator *a);

#ifndef UNIT_TEST_ENVIRONMENT

static inline void *kmalloc(size_t size)
{
   return general_kmalloc(&size, 0);
}

static inline void kfree2(void *ptr, size_t size)
{
   general_kfree(ptr, &size, 0);
}

#else

void *kmalloc(size_t size);
void kfree2(void *ptr, size_t user_size);

#endif

static inline void *kzmalloc(size_t size)
{
   void *res = kmalloc(size);

   if (!res)
      return NULL;

   bzero(res, size);
   return res;
}

size_t kmalloc_get_heap_struct_size(void);

void *mdalloc(size_t size);
void mdfree(void *b);


/* kmalloc debug helpers */

void debug_kmalloc_dump_mem_usage(void);
void debug_kmalloc_start_leak_detector(bool save_metadata);
void debug_kmalloc_stop_leak_detector(bool show_leaks);

void debug_kmalloc_start_log(void);
void debug_kmalloc_stop_log(void);
