/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#define KMALLOC_METADATA_BLOCK_NODE_SIZE  (1)
#define KMALLOC_HEAPS_COUNT              (32)

#define KMALLOC_FL_MULTI_STEP               (0b10000000000000000000000000000000)
#define KMALLOC_FL_NO_ACTUAL_ALLOC          (0b01000000000000000000000000000000)
#define KMALLOC_FL_ALIGN_TYPE_MASK          (0b00110000000000000000000000000000)
#define KMALLOC_FL_ALIGN_2PTR_SIZE          (0b00000000000000000000000000000000)
#define KMALLOC_FL_ALIGN_4PTR_SIZE          (0b00010000000000000000000000000000)
#define KMALLOC_FL_ALIGN_8PTR_SIZE          (0b00100000000000000000000000000000)
#define KMALLOC_FL_ALIGN_16PTR_SIZE         (0b00110000000000000000000000000000)
#define KMALLOC_FL_DONT_ACCOUNT             (0b00001000000000000000000000000000)
#define KMALLOC_FL_SUB_BLOCK_MIN_SIZE_MASK  (0b00000111111111111111111111111111)

#define KFREE_FL_MULTI_STEP                 (0b10000000000000000000000000000000)
#define KFREE_FL_NO_ACTUAL_FREE             (0b01000000000000000000000000000000)
#define KFREE_FL_ALLOW_SPLIT                (0b00100000000000000000000000000000)

typedef bool (*virtual_alloc_and_map_func)(uptr vaddr, size_t page_count);
typedef void (*virtual_free_and_unmap_func)(uptr vaddr, size_t page_count);

typedef struct kmalloc_heap kmalloc_heap;

#define calculate_heap_metadata_size(heap_size, min_block_size) \
   (2 * (heap_size) / (min_block_size))

#define calculate_heap_min_block_size(heap_size, metadata_size) \
   (2 * (heap_size) / (metadata_size))


void init_kmalloc(void);

void *general_kmalloc(size_t *size, u32 flags);
void general_kfree(void *ptr, size_t *size, u32 flags);
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

static inline void *aligned_kmalloc(size_t size, u32 align_flags)
{
   return general_kmalloc(&size, align_flags & KMALLOC_FL_ALIGN_TYPE_MASK);
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
size_t kmalloc_get_max_tot_heap_free(void);

/*
 * kmalloc() wrapper which prefixes the alloc block with metadata containing
 * block's size. It's better to avoid this as much as possible, but it's super
 * useful when, no matter what, the allocated object has variable size and must
 * contain a `size` field, just in order to it to be passed to kfree2(). In such
 * cases, it's totally worth just calling `mdalloc()` and freeing the block with
 * `mkfree()`, without needing to keep explicitly an otherwise-unnecessary size
 * field.
 *
 * NOTE: blocks allocated with this function MUST BE freed using `mdfree()`.
 */
void *mdalloc(size_t size);

/*
 * Counter-part of `mdalloc()`.
 *
 * It can only free blocks allocated with `mdalloc()`. Trying to free a regular
 * block allocated directly with kmalloc(), will end up in underfined behavior.
 * In debug builds, there's a check against a magic number.
 */
void mdfree(void *b);
