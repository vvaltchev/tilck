
#pragma once

#include <common_defs.h>
#include <string_util.h>

#if !defined(TESTING) && !defined(KERNEL_TEST)
#define HEAP_BASE_ADDR (KERNEL_BASE_VA + 64 * MB)
#define HEAP_DATA_SIZE (64 * MB)
#else
extern void *kernel_heap_base;
#define HEAP_BASE_ADDR ((uptr)kernel_heap_base)
#define HEAP_DATA_SIZE (64 * MB)
#endif

#define MIN_BLOCK_SIZE (32)
#define ALLOC_BLOCK_PAGES (32)
#define ALLOC_BLOCK_SIZE (ALLOC_BLOCK_PAGES * PAGE_SIZE)


#define KMALLOC_METADATA_BLOCK_NODE_SIZE (1)
#define KMALLOC_NODES_COUNT_IN_META_DATA (2 * HEAP_DATA_SIZE / MIN_BLOCK_SIZE)


#define KM_METADATA_ALLOC_BS_SHIFT 15 // 2^15 = 32 K
#define KM_METADATA_ALLOC_BS (1 << KM_METADATA_ALLOC_BS_SHIFT)

#define KMALLOC_METADATA_SIZE \
   (KMALLOC_METADATA_BLOCK_NODE_SIZE * KMALLOC_NODES_COUNT_IN_META_DATA /   \
    KM_METADATA_ALLOC_BS)



#define HEAP_DATA_ADDR                                                    \
   (HEAP_BASE_ADDR +                                                      \
    KMALLOC_NODES_COUNT_IN_META_DATA * KMALLOC_METADATA_BLOCK_NODE_SIZE)

void initialize_kmalloc();
void *kmalloc(size_t size);
void kfree(void *ptr, size_t size);

static inline void *kzmalloc(size_t size)
{
   void *res = kmalloc(size);
   bzero(res, size);
   return res;
}
