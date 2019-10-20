/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/bintree.h>

typedef struct {

   uptr vaddr;
   size_t size;
   size_t mem_allocated;
   size_t min_block_size;
   size_t alloc_block_size;
   int region;

} debug_kmalloc_heap_info;

typedef struct {

   int tot_count;
   int peak_count;
   int not_full_count;
   int peak_not_full_count;
   int empty_count;
   int lifetime_created_heaps_count;

} kmalloc_small_heaps_stats;

typedef struct {

   struct bintree_walk_ctx ctx;

} debug_kmalloc_chunks_ctx;

typedef struct {

   kmalloc_small_heaps_stats small_heaps;
   size_t chunk_sizes_count;

} debug_kmalloc_stats;

bool debug_kmalloc_get_heap_info(int heap_num, debug_kmalloc_heap_info *i);
void debug_kmalloc_get_stats(debug_kmalloc_stats *stats);
void debug_kmalloc_chunks_stats_start_read(debug_kmalloc_chunks_ctx *ctx);
bool debug_kmalloc_chunks_stats_next(debug_kmalloc_chunks_ctx *ctx,
                                     size_t *size, size_t *count);

void debug_kmalloc_start_leak_detector(bool save_metadata);
void debug_kmalloc_stop_leak_detector(bool show_leaks);

void debug_kmalloc_start_log(void);
void debug_kmalloc_stop_log(void);
