/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/mods/tracing.h>

static bool kmalloc_debug_log;

#if KMALLOC_SUPPORT_DEBUG_LOG
   #define DEBUG_COND (kmalloc_debug_log)
   #define DEBUG_printk(...) if (DEBUG_COND) trace_printk(80, __VA_ARGS__)
#else
   #define DEBUG_printk(...) if (0) trace_printk(80, __VA_ARGS__)
#endif


void debug_kmalloc_start_log(void)
{
   if (!KMALLOC_SUPPORT_DEBUG_LOG)
      panic("kmalloc debug logs funcs are NOT compiled in");

   kmalloc_debug_log = true;
}

void debug_kmalloc_stop_log(void)
{
   if (!KMALLOC_SUPPORT_DEBUG_LOG)
      panic("kmalloc debug logs funcs are NOT compiled in");

   kmalloc_debug_log = false;
}


#define DEBUG_stop_coaleshe                                                 \
                                                                            \
   DEBUG_printk("STOP: unable to mark node #%i (size %zu) as free",         \
                n, curr_size);                                              \
                                                                            \
   DEBUG_printk("node left:  full: %i, split: %i",                          \
                left.full, left.split);                                     \
                                                                            \
   DEBUG_printk("node right: full: %i, split: %i",                          \
                right.full, left.split)                                     \

#define DEBUG_coaleshe                                                      \
   DEBUG_printk("Marking node #%i (size: %zu) as free", n, curr_size)

#define DEBUG_allocate_node1                                                \
   DEBUG_printk("For node #%i, using alloc block (%i/%zu): %p (node #%u)",  \
                ptr_to_node(h, (void *)vaddr, node_size), i+1,              \
                alloc_block_count, TO_PTR(alloc_block_vaddr), alloc_node)   \

#define DEBUG_allocate_node2                                                \
   DEBUG_printk("Allocating block of pages..")

#define DEBUG_allocate_node3                                                \
   DEBUG_printk("Returning addr %p (%zu alloc blocks)",                     \
                TO_PTR(vaddr),                                              \
                alloc_block_count)                                          \

#define DEBUG_kmalloc_begin                                                 \
   DEBUG_printk("kmalloc(%zu)...", *size)

#define DEBUG_kmalloc_call_begin                                            \
   DEBUG_printk("Node #%i, node_size = %zu, vaddr = %p",                    \
                node, node_size, node_to_ptr(h, node, node_size))           \

#define DEBUG_kmalloc_bad_end                                               \
   DEBUG_printk("kmalloc_bad_end: ptr: %p, node #%i, size: %zu",            \
                vaddr, node, size)                                          \

#define DEBUG_kmalloc_end                                                   \
   DEBUG_printk("kmalloc_end: ptr: %p, node #%i, size: %zu",                \
                vaddr, node, size)                                          \

#define DEBUG_already_full                                                  \
   DEBUG_printk("Already FULL, return NULL")

#define DEBUG_already_split                                                 \
   DEBUG_printk("Already split, return NULL")

#define DEBUG_kmalloc_split                                                 \
   DEBUG_printk("Splitting node #%u...", node)

#define DEBUG_going_left                                                    \
   DEBUG_printk("going to left..")

#define DEBUG_left_failed                                                   \
   DEBUG_printk("allocation on left node not possible, trying with right..")

#define DEBUG_going_right                                                   \
   DEBUG_printk("going on right..")

#define DEBUG_right_failed                                                  \
   DEBUG_printk("allocation on right node was not possible, return NULL.")

#define DEBUG_free1                                                         \
   DEBUG_printk("kfree: ptr: %p, node #%i (size %zu)", ptr, node, size)

#define DEBUG_free_after_coaleshe                                           \
   DEBUG_printk("After coaleshe, biggest_free_node #%i, "                   \
                "biggest_free_size = %zu",                                  \
                biggest_free_node, biggest_free_size)                       \

#define DEBUG_free_alloc_block_count                                        \
   DEBUG_printk("The block node used up to %zu pages", alloc_block_count)


#define DEBUG_check_alloc_block                                             \
   DEBUG_printk("Checking alloc block i = %i, pNode = %i, pAddr = %p, "     \
                 "alloc = %i, free = %i, split = %i",                       \
                 i, alloc_node, TO_PTR(alloc_block_vaddr),                  \
                 nodes[alloc_node].allocated,                               \
                 !nodes[alloc_node].full,                                   \
                 nodes[alloc_node].split)                                   \

#define DEBUG_free_freeing_block                                            \
   DEBUG_printk("---> FREEING the ALLOC BLOCK!")

#define DEBUG_free_skip_alloc_failed_block                                  \
   DEBUG_printk("---> SKIP alloc-failed ALLOC BLOCK!")
