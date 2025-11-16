/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>

#include <tilck/kernel/list.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/kmalloc_debug.h>

#include <tilck_gen_headers/config_kmalloc.h>

#include "kmalloc_heap_struct.h"

/*
 * NOTE: the trick to make the small heap to work well without the number of
 * small heaps to explode is to allow it to allocate just a small fraction of
 * its actual size, like 1/16th.
 */
#define SMALL_HEAP_MAX_ALLOC (SMALL_HEAP_SIZE / 16 - 1)

#define SMALL_HEAP_NODE_ALLOC_SZ \
   MAX(sizeof(struct small_heap_node), SMALL_HEAP_MAX_ALLOC + 1)

/*
 * Be careful with this: incrementing its value to 2 does not reduce with any of
 * the current tests the value of shs.lifetime_created_heaps_count: this means
 * just wasting memory. While, setting it to 0, increases that by +1. It's a
 * good parameter to have and evaluate it's effects in the long term, with more
 * and more complex uses cases.
 */

#if !KERNEL_TEST_INT
   #define MAX_EMPTY_SMALL_HEAPS    1
#else
   #define MAX_EMPTY_SMALL_HEAPS    0
#endif



struct small_heap_node {
   struct list_node node;          /* all nodes */
   struct list_node avail_node;    /* non-full nodes, including empty ones */
   struct kmalloc_heap heap;
};

STATIC bool kmalloc_initialized;
STATIC struct kmalloc_heap first_heap_struct;
STATIC struct kmalloc_heap *heaps[KMALLOC_HEAPS_COUNT];
STATIC int used_heaps;
STATIC size_t max_tot_heap_mem_free;
static struct kmalloc_small_heaps_stats shs;
static struct list small_heaps_list;
static struct list avail_small_heaps_list;
static size_t alloc_arr_used;

static void kmalloc_account_alloc(size_t size);
STATIC_INLINE int ptr_to_node(struct kmalloc_heap *h, void *ptr, size_t size);
STATIC_INLINE void *node_to_ptr(struct kmalloc_heap *h, int node, size_t size);
static void kmalloc_init_heavy_stats(void);
static void *small_heaps_kmalloc(size_t *size, u32 flags);
static int small_heaps_kfree(void *ptr, size_t *size, u32 flags);

