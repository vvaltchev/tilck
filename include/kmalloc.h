
#pragma once

#include <commonDefs.h>

#if !defined(TESTING) && !defined(KERNEL_TEST)
#define HEAP_BASE_ADDR (KERNEL_BASE_VA + 0x4000000) // BASE + 64 MB
#define HEAP_DATA_SIZE (64 * 1024 * 1024)
#else
extern void *kernel_heap_base;
#define HEAP_BASE_ADDR ((uptr)kernel_heap_base)
#define HEAP_DATA_SIZE (64 * 1024 * 1024)
#endif

#define MIN_BLOCK_SIZE (32)


void initialize_kmalloc();
void *kmalloc(size_t size);
void kfree(void *ptr, size_t size);


#define KMALLOC_METADATA_BLOCK_NODE_SIZE (1)
#define KMALLOC_NODES_COUNT_IN_META_DATA (2 * HEAP_DATA_SIZE / MIN_BLOCK_SIZE)

#define HEAP_DATA_ADDR                                                    \
   (HEAP_BASE_ADDR +                                                      \
    KMALLOC_NODES_COUNT_IN_META_DATA * KMALLOC_METADATA_BLOCK_NODE_SIZE)
