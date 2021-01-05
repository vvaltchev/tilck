/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>

#define KMALLOC_METADATA_BLOCK_NODE_SIZE      1
#define KMALLOC_HEAPS_COUNT                  32

#define SMALL_HEAP_MBS                       32
#define SMALL_HEAP_SIZE               (32 * KB)

#define SMALL_HEAP_MD_SIZE \
   (calculate_heap_metadata_size(SMALL_HEAP_SIZE, SMALL_HEAP_MBS))

#define KMALLOC_FL_MULTI_STEP               (0b10000000000000000000000000000000)
#define KMALLOC_FL_NO_ACTUAL_ALLOC          (0b01000000000000000000000000000000)
#define KMALLOC_FL_DMA                      (0b00100000000000000000000000000000)
#define KMALLOC_FL_RESV_FLAGS_MASK          (0b00010000000000000000000000000000)
#define KMALLOC_FL_DONT_ACCOUNT             (0b00001000000000000000000000000000)
#define KMALLOC_FL_SUB_BLOCK_MIN_SIZE_MASK  (0b00000111111111111111111111111111)

#define KFREE_FL_MULTI_STEP                 (0b10000000000000000000000000000000)
#define KFREE_FL_NO_ACTUAL_FREE             (0b01000000000000000000000000000000)
#define KFREE_FL_ALLOW_SPLIT                (0b00100000000000000000000000000000)

typedef bool (*virtual_alloc_and_map_func)(ulong vaddr, size_t page_count);
typedef void (*virtual_free_and_unmap_func)(ulong vaddr, size_t page_count);

#define calculate_heap_metadata_size(heap_size, min_block_size) \
   (2 * (heap_size) / (min_block_size))

#define calculate_heap_min_block_size(heap_size, metadata_size) \
   (2 * (heap_size) / (metadata_size))

struct kmalloc_heap;

void
early_init_kmalloc(void);

void
init_kmalloc(void);

void *
general_kmalloc(size_t *size, u32 flags);

void
general_kfree(void *ptr, size_t *size, u32 flags);

bool
is_kmalloc_initialized(void);

bool
kmalloc_create_heap(struct kmalloc_heap *h,
                    ulong vaddr,
                    size_t size,
                    size_t min_block_size,
                    size_t alloc_block_size, /* 0 if linear_mapping=1 */
                    bool linear_mapping,
                    void *metadata_nodes,               // optional
                    virtual_alloc_and_map_func valloc,  // optional
                    virtual_free_and_unmap_func vfree); // optional

struct kmalloc_heap *
kmalloc_create_regular_heap(ulong vaddr,
                            size_t size,
                            size_t min_block_size);


void
kmalloc_destroy_heap(struct kmalloc_heap *h);

struct kmalloc_heap *
kmalloc_heap_dup(struct kmalloc_heap *h);

struct kmalloc_heap *
kmalloc_heap_dup_expanded(struct kmalloc_heap *h, size_t sz);

void *
per_heap_kmalloc(struct kmalloc_heap *h, size_t *size, u32 flags);

void
per_heap_kfree(struct kmalloc_heap *h, void *ptr, size_t *size, u32 flags);

struct kmalloc_acc {

   u32 elem_size;
   u32 elem_count;
   u32 curr_elem;
   char *buf;
};

void
kmalloc_create_accelerator(struct kmalloc_acc *a, u32 elem_s, u32 elem_c);

void *
kmalloc_accelerator_get_elem(struct kmalloc_acc *a);

void
kmalloc_destroy_accelerator(struct kmalloc_acc *a);

#ifndef UNIT_TEST_ENVIRONMENT

static inline void *
kmalloc(size_t size)
{
   return general_kmalloc(&size, 0);
}

static inline void
kfree2(void *ptr, size_t size)
{
   general_kfree(ptr, &size, 0);
}

#else

void *
kmalloc(size_t size);

void
kfree2(void *ptr, size_t user_size);

#endif

void *
kzmalloc(size_t size);

size_t
kmalloc_get_heap_struct_size(void);

size_t
kmalloc_get_max_tot_heap_free(void);

void *
aligned_kmalloc(size_t size, u32 align);

void
aligned_kfree2(void *ptr, size_t size);

void *
vmalloc(size_t size);

void
vfree2(void *ptr, size_t size);

/* Free function to use when we really don't know chunk's size */
static inline void
kfree(void *ptr)
{
   kfree2(ptr, 0);
}

#define kalloc_obj(type)                 kmalloc(sizeof(type))
#define kzalloc_obj(type)                kzmalloc(sizeof(type))
#define kfree_obj(ptr, type)             kfree2((ptr), sizeof(type))
#define kalloc_array_obj(type, cnt)      kmalloc(sizeof(type) * (cnt))
#define kzalloc_array_obj(type, cnt)     kzmalloc(sizeof(type) * (cnt))
#define kfree_array_obj(ptr, type, cnt)  kfree2((ptr), sizeof(type) * (cnt))
