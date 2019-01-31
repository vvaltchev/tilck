/* SPDX-License-Identifier: BSD-2-Clause */

#define _KMALLOC_C_

#include <tilck/common/string_util.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/sort.h>

#include "kmalloc_debug.h"
#include "kmalloc_heap_struct.h"
#include "kmalloc_block_node.h"

size_t kmalloc_get_heap_struct_size(void)
{
   return sizeof(kmalloc_heap);
}

STATIC_ASSERT(sizeof(block_node) == KMALLOC_METADATA_BLOCK_NODE_SIZE);

STATIC bool kmalloc_initialized;
static const block_node s_new_node; // Just zeros.

#define HALF(x) ((x) >> 1)
#define TWICE(x) ((x) << 1)

#define NODE_LEFT(n) (TWICE(n) + 1)
#define NODE_RIGHT(n) (TWICE(n) + 2)
#define NODE_PARENT(n) (HALF(n-1))
#define NODE_IS_LEFT(n) (((n) & 1) != 0)

bool is_kmalloc_initialized(void)
{
   return kmalloc_initialized;
}

STATIC_INLINE int ptr_to_node(kmalloc_heap *h, void *ptr, size_t size)
{
   const uptr size_log = log2_for_power_of_2(size);

   const uptr offset = (uptr)ptr - h->vaddr;
   const uptr nodes_before_our = (1 << (h->heap_data_size_log2 - size_log)) - 1;
   const uptr position_in_row = offset >> size_log;

   return (int)(nodes_before_our + position_in_row);
}

STATIC_INLINE void *node_to_ptr(kmalloc_heap *h, int node, size_t size)
{
   const uptr size_log = log2_for_power_of_2(size);

   const uptr nodes_before_our = (1 << (h->heap_data_size_log2 - size_log)) - 1;
   const uptr position_in_row = (u32)node - nodes_before_our;
   const uptr offset = position_in_row << size_log;

   return (void *)(offset + h->vaddr);
}

CONSTEXPR static ALWAYS_INLINE bool is_block_node_free(block_node n)
{
   return !(n.raw & (FL_NODE_FULL | FL_NODE_SPLIT));
}

static size_t set_free_uplevels(kmalloc_heap *h, int *node, size_t size)
{
   block_node *nodes = h->metadata_nodes;

   size_t curr_size = size << 1;
   int n = *node;

   ASSERT(!nodes[n].split);

   nodes[n].full = false;
   n = NODE_PARENT(n);

   while (!is_block_node_free(nodes[n])) {

      block_node left = nodes[NODE_LEFT(n)];
      block_node right = nodes[NODE_RIGHT(n)];

      if (!is_block_node_free(left) || !is_block_node_free(right)) {
         DEBUG_stop_coaleshe;
         nodes[n].full = left.full && right.full;
         ASSERT(nodes[n].split);
         ASSERT((left.full && right.full && nodes[n].full) || !nodes[n].full);
         curr_size >>= 1;
         break;
      }

      *node = n; // last successful coaleshe.

      DEBUG_coaleshe;
      nodes[n].raw &= ~(FL_NODE_SPLIT | FL_NODE_FULL);

      if (n == 0)
         break; /* we processed the root node, cannot go further */

      n = NODE_PARENT(n);
      curr_size <<= 1;
   }

   /*
    * We have coaleshed as much nodes as possible, now we have to continue
    * up to the root just to mark the higher nodes as NOT full, even if they
    * cannot be coaleshed.
    */

   while (n >= 0) {
      nodes[n].full = false;
      n = NODE_PARENT(n);
   }

   return curr_size;
}

static bool
actual_allocate_node(kmalloc_heap *h,
                     size_t node_size,
                     int node,
                     void **vaddr_ref,
                     bool do_actual_alloc)
{
   bool alloc_failed = false;
   block_node *nodes = h->metadata_nodes;
   nodes[node].full = true;

   const uptr vaddr = (uptr)node_to_ptr(h, node, node_size);
   *vaddr_ref = (void *)vaddr;

   if (h->linear_mapping || !do_actual_alloc)
      return true; // nothing to do!

   uptr alloc_block_vaddr = vaddr & ~(h->alloc_block_size - 1);
   const u32 alloc_block_count =
      1 + ((node_size - 1) >> h->alloc_block_size_log2);

   /*
    * Code dealing with the tricky allocation logic.
    */

   for (u32 i = 0; i < alloc_block_count; i++) {

      int alloc_node = ptr_to_node(h,
                                   (void *)alloc_block_vaddr,
                                   h->alloc_block_size);

      ASSERT(node_to_ptr(h, alloc_node,
                         h->alloc_block_size) == (void *)alloc_block_vaddr);

      DEBUG_allocate_node1;

      /*
       * The nodes with alloc_failed=1 are supposed to be processed immediately
       * by internal_kfree() after actual_allocate_node() failed.
       */

      ASSERT(!nodes[alloc_node].alloc_failed);

      if (!nodes[alloc_node].allocated) {

         DEBUG_allocate_node2;

         bool success =
            h->valloc_and_map(alloc_block_vaddr,
                              h->alloc_block_size / PAGE_SIZE);

         if (success) {
            nodes[alloc_node].allocated = true;
         } else {
            nodes[alloc_node].alloc_failed = true;
            alloc_failed = true;
         }
      }

      if (node_size >= h->alloc_block_size) {
         ASSERT(!nodes[alloc_node].split);
         nodes[alloc_node].full = true;
      } else {
         ASSERT(nodes[alloc_node].split);
      }

      alloc_block_vaddr += h->alloc_block_size;
   }

   DEBUG_allocate_node3;
   return !alloc_failed;
}

static size_t calculate_node_size(kmalloc_heap *h, int node)
{
   size_t size = h->size;

   for (int cn = node; cn != 0; cn = NODE_PARENT(cn)) {
      size >>= 1;
   }

   return size;
}

void
internal_kmalloc_split_block(kmalloc_heap *h,
                             void *const vaddr,
                             const size_t block_size,
                             const size_t leaf_node_size)
{
   ASSERT(leaf_node_size >= h->min_block_size);
   ASSERT(block_size >= h->min_block_size);
   ASSERT(roundup_next_power_of_2(leaf_node_size) == leaf_node_size);
   ASSERT(roundup_next_power_of_2(block_size) == block_size);

   block_node *nodes = h->metadata_nodes;

   size_t s;
   int n = ptr_to_node(h, vaddr, block_size);
   int node_count = 1;

   ASSERT(nodes[n].full);
   ASSERT(!nodes[n].split);

   for (s = block_size; s > leaf_node_size; s >>= 1) {

      if (s != h->alloc_block_size) {
         memset(&nodes[n], FL_NODE_SPLIT | FL_NODE_FULL, (size_t)node_count);
      } else {
         for (int j = 0; j < node_count; j++)
            nodes[n + j].raw |= (FL_NODE_SPLIT | FL_NODE_FULL);
      }

      node_count <<= 1;
      n = NODE_LEFT(n);
   }

   ASSERT(s == leaf_node_size);

   if (s != h->alloc_block_size) {
      memset(&nodes[n], FL_NODE_FULL, (size_t)node_count);
   } else {
      for (int j = 0; j < node_count; j++) {
         nodes[n + j].full = 1;
         nodes[n + j].split = 0;
      }
   }
}

size_t
internal_kmalloc_coalesce_block(kmalloc_heap *h,
                                void *const vaddr,
                                const size_t block_size)
{
   block_node *nodes = h->metadata_nodes;
   const int block_node_num = ptr_to_node(h, vaddr, block_size);

   size_t s;
   int n = block_node_num;
   int node_count = 1;
   size_t already_free_size = 0;

   ASSERT(nodes[n].full || nodes[n].split);

   for (s = block_size; s >= h->min_block_size; s >>= 1) {

      if (s > h->min_block_size) {

         if (s != h->alloc_block_size) {
            bzero(&nodes[n], (size_t)node_count);
         } else {
            for (int j = n; j < n + node_count; j++)
               nodes[j].raw &= ~(FL_NODE_SPLIT | FL_NODE_FULL);
         }

      } else {

         for (int j = n; j < n + node_count; j++) {

            if (!(nodes[j].raw & (FL_NODE_SPLIT | FL_NODE_FULL)))
               already_free_size += s;

            nodes[j].raw &= ~(FL_NODE_SPLIT | FL_NODE_FULL);
         }
      }

      node_count <<= 1;
      n = NODE_LEFT(n);
   }

   nodes[block_node_num].full = 1;
   ASSERT(s < h->min_block_size);
   return already_free_size;
}

static void *
internal_kmalloc(kmalloc_heap *h,
                 const size_t size,       /* power of 2 */
                 const int start_node,
                 size_t start_node_size,
                 bool mark_node_as_allocated,
                 bool do_actual_alloc)   /* ignored if linear_mapping = 1 */
{
   /*
    * do_actual_alloc -> mark_node_as_allocated (logical implication).
    */
   ASSERT(!do_actual_alloc || mark_node_as_allocated);

   block_node *nodes = h->metadata_nodes;
   int stack_size = 0;

   if (!start_node_size)
      start_node_size = calculate_node_size(h, start_node);

   SIMULATE_CALL2(start_node_size, start_node);

   while (stack_size) {

      // Load the "stack" (function arguments)
      const size_t node_size = LOAD_ARG_FROM_STACK(1, size_t);
      const int node = LOAD_ARG_FROM_STACK(2, int);

      HANDLE_SIMULATED_RETURN();

      // Handle a SIMULATED "call"
      DEBUG_kmalloc_call_begin;

      block_node n = nodes[node];

      if (n.full) {
         DEBUG_already_full;
         SIMULATE_RETURN_NULL();
      }

      if (HALF(node_size) < size) {

         if (n.split) {
            DEBUG_already_split;
            SIMULATE_RETURN_NULL();
         }

         void *vaddr = NULL;
         bool success;

         if (mark_node_as_allocated) {
            success = actual_allocate_node(h, node_size,
                                           node, &vaddr, do_actual_alloc);
            ASSERT(vaddr != NULL); // 'vaddr' is not NULL even when !success
         } else {
            success = true;
            vaddr = node_to_ptr(h, node, node_size);
         }

         // Mark the parent nodes as 'full', when necessary.

         for (int ss = stack_size - 2; ss >= 0; ss--) {

            const int nn = (int)(sptr) STACK_VAR[ss].arg2; /* arg2: node */

            if (nodes[NODE_LEFT(nn)].full && nodes[NODE_RIGHT(nn)].full) {
               ASSERT(!nodes[nn].full);
               nodes[nn].full = true;
            }
         }

         if (UNLIKELY(!success)) {

            /*
             * Corner case: in case of non-linearly mapped heaps, a successfull
             * allocation in the heap metadata does not always mean a sucessfull
             * kmalloc(), because the underlying allocator [h->valloc_and_map]
             * might have failed. In this case we have to call per_heap_kfree
             * and restore kmalloc's heap metadata to the previous state. Also,
             * all the alloc nodes we be either marked as allocated or
             * alloc_failed.
             */

            DEBUG_kmalloc_bad_end;
            size_t actual_size = node_size;
            per_heap_kfree(h, vaddr, &actual_size, 0);
            return NULL;
         }

         DEBUG_kmalloc_end;

         if (do_actual_alloc)
            h->mem_allocated += node_size;

         return vaddr;
      }

      if (!n.split) {
         DEBUG_kmalloc_split;

         nodes[node].split = true;
         nodes[NODE_LEFT(node)].raw &= ~(FL_NODE_SPLIT & FL_NODE_FULL);
         nodes[NODE_RIGHT(node)].raw &= ~(FL_NODE_SPLIT & FL_NODE_FULL);
      }

      if (!nodes[NODE_LEFT(node)].full) {

         DEBUG_going_left;
         SIMULATE_CALL2(HALF(node_size), NODE_LEFT(node));

         /*
          * If we got here, the "call" on the left node "returned" NULL so,
          * we have to try go to the right node. [In case of success, this
          * function returns directly.]
          */

         DEBUG_left_failed;
      }

      if (!nodes[NODE_RIGHT(node)].full) {
         DEBUG_going_right;
         SIMULATE_CALL2(HALF(node_size), NODE_RIGHT(node));

         /*
          * When the above "call" succeeds, we don't get here. When it fails,
          * we get here and just continue the execution.
          */
         DEBUG_right_failed;
      }

      // In case both the nodes are full, just return NULL.
      SIMULATE_RETURN_NULL();
      NOREC_LOOP_END();
   }

   return NULL;
}

void *
per_heap_kmalloc(kmalloc_heap *h, size_t *size, u32 flags)
{
   void *addr;
   const bool multi_step_alloc = !!(flags & KMALLOC_FL_MULTI_STEP);
   const bool do_actual_alloc = !(flags & KMALLOC_FL_NO_ACTUAL_ALLOC);
   const u32 sub_blocks_min_size = flags & KMALLOC_FL_SUB_BLOCK_MIN_SIZE_MASK;

   ASSERT(*size != 0);
   ASSERT(!sub_blocks_min_size || sub_blocks_min_size >= h->min_block_size);
   ASSERT(!is_preemption_enabled());

   DEBUG_kmalloc_begin;

   if (UNLIKELY(*size > h->size))
      return NULL;

   const size_t rounded_up_size =
      MAX(roundup_next_power_of_2(*size), h->min_block_size);

   if (!multi_step_alloc || ((rounded_up_size - *size) < h->min_block_size)) {

      *size = rounded_up_size;

      addr = internal_kmalloc(h,          /* heap */
                              *size,      /* block size */
                              0,          /* start node */
                              h->size,    /* start node size */
                              true,       /* mark node as allocated */
                              do_actual_alloc);

      if (sub_blocks_min_size && addr) {
         internal_kmalloc_split_block(h, addr, *size, sub_blocks_min_size);
      }

      return addr;
   }

   /*
    * multi_step_alloc is true, therefore we can do multiple allocations in
    * order to allocate almost exactly (round-up at min_block_size) bytes.
    *
    * NOTE: we don't have to mark as free the remaining of big_block after the
    * "for" loop below, because we called internal_kmalloc() with
    * do_actual_alloc = false.
    */

   const size_t desired_size = *size;
   void *big_block =
      internal_kmalloc(h, rounded_up_size, 0, h->size, false, false);

   if (!big_block)
      return NULL;

   const int big_block_node = ptr_to_node(h, big_block, rounded_up_size);
   size_t tot = 0;

   for (int i=(int)h->heap_data_size_log2-1; i >= 0 && tot < desired_size; i--)
   {
      const size_t s = (1u << i);

      if (!(desired_size & s))
         continue;

      addr = internal_kmalloc(h,
                              s,
                              big_block_node,
                              rounded_up_size,
                              true,              /* mark node as allocated */
                              do_actual_alloc);

      ASSERT(addr == big_block + tot);

      if (sub_blocks_min_size) {
         internal_kmalloc_split_block(h, addr, s, sub_blocks_min_size);
      }

      tot += s;
   }

   *size = tot;
   return big_block;
}


static void
internal_kfree(kmalloc_heap *h,
               void *ptr,
               size_t size,
               bool allow_split,
               bool do_actual_free)
{
   const int node = ptr_to_node(h, ptr, size);
   size_t free_size_correction = 0;

   DEBUG_free1;
   ASSERT(node_to_ptr(h, node, size) == ptr);
   ASSERT(roundup_next_power_of_2(size) == size);
   ASSERT(size >= h->min_block_size);

   block_node *nodes = h->metadata_nodes;

   if (allow_split && nodes[node].split) {
      free_size_correction = internal_kmalloc_coalesce_block(h, ptr, size);
   }

   h->mem_allocated -= (size - free_size_correction);

   /*
    * Regular calls to kfree2() pass to it "regular" blocks returned by
    * kmalloc(), which are never split.
    */
   ASSERT(!nodes[node].split);

   {
      int biggest_free_node = node;
      // Mark the parent nodes as free, when necessary.
      size_t biggest_free_size = set_free_uplevels(h, &biggest_free_node, size);

      DEBUG_free_after_coaleshe;

      ASSERT(biggest_free_node == node || biggest_free_size != size);

      if (biggest_free_size < h->alloc_block_size)
         return;
   }

   if (h->linear_mapping || !do_actual_free)
      return; // nothing to do!

   uptr alloc_block_vaddr = (uptr)ptr & ~(h->alloc_block_size - 1);
   const u32 alloc_block_count = 1 + ((size - 1) >> h->alloc_block_size_log2);

   /*
    * Code dealing with the tricky allocation logic.
    */

   DEBUG_free_alloc_block_count;

   for (u32 i = 0; i < alloc_block_count; i++) {

      const int alloc_node =
         ptr_to_node(h, (void *)alloc_block_vaddr, h->alloc_block_size);

      DEBUG_check_alloc_block;

      /*
       * For nodes smaller than h->alloc_block_size, the page we're freeing MUST
       * be free. For bigger nodes that kind of checking does not make sense:
       * a major block owns its all pages and their flags are irrelevant.
       */
      ASSERT(size >= h->alloc_block_size ||
             is_block_node_free(nodes[alloc_node]));

      if (!allow_split) {
         ASSERT(nodes[alloc_node].allocated || nodes[alloc_node].alloc_failed);
      }

      if (nodes[alloc_node].allocated) {
         DEBUG_free_freeing_block;
         h->vfree_and_unmap(alloc_block_vaddr, h->alloc_block_size / PAGE_SIZE);
         nodes[alloc_node] = s_new_node;
      } else if (nodes[alloc_node].alloc_failed) {
         DEBUG_free_skip_alloc_failed_block;
         nodes[alloc_node].alloc_failed = false;
      }

      alloc_block_vaddr += h->alloc_block_size;
   }
}

static size_t calculate_block_size(kmalloc_heap *h, uptr vaddr)
{
   block_node *nodes = h->metadata_nodes;
   int n = 0; /* root's node index */
   uptr va = h->vaddr; /* root's node data address == heap's address */
   size_t size = h->size; /* root's node size == heap's size */

   while (size > h->min_block_size) {

      if (!nodes[n].split)
         break;

      size >>= 1;

      if (vaddr >= (va + size)) {
         va += size;
         n = NODE_RIGHT(n);
      } else {
         n = NODE_LEFT(n);
      }
   }

   return size;
}

static void
debug_check_block_size(kmalloc_heap *h, uptr vaddr, size_t size)
{
   size_t cs = calculate_block_size(h, vaddr);

   if (cs != size) {
      panic("calculated_size[%u] != user_size[%u] for block at: %p\n",
            cs, size, vaddr);
   }
}

void
per_heap_kfree(kmalloc_heap *h, void *ptr, size_t *user_size, u32 flags)
{
   if (!ptr)
      return;

   size_t size;
   const bool allow_split = !!(flags & KFREE_FL_ALLOW_SPLIT);
   const bool multi_step_free = !!(flags & KFREE_FL_MULTI_STEP);
   const bool do_actual_free = !(flags & KFREE_FL_NO_ACTUAL_FREE);

   DEBUG_ONLY(uptr vaddr = (uptr)ptr);
   ASSERT(!is_preemption_enabled());

   ASSERT(vaddr >= h->vaddr);
   ASSERT(*user_size);

   if (!multi_step_free) {

      size = roundup_next_power_of_2(MAX(*user_size, h->min_block_size));

      if (!allow_split) {
         DEBUG_ONLY(debug_check_block_size(h, vaddr, size));
      }

      *user_size = size;
      ASSERT(vaddr + size <= h->heap_over_end);
      return internal_kfree(h, ptr, size, allow_split, do_actual_free);
   }

   size = *user_size; /*
                       * No round-up: when multi_step_free=1 we assume that the
                       * size is the one returned as an out-param by
                       * per_heap_kmalloc(). Also, we don't have to alter
                       * *user_size too.
                       */

   ASSERT(vaddr + size <= h->heap_over_end);
   ASSERT(round_up_at(size, h->min_block_size) == size);

   size_t tot = 0;

   for (int i = (int)h->heap_data_size_log2 - 1; i >= 0 && tot < size; i--) {

      const size_t sub_block_size = (1 << i);

      if (!(size & sub_block_size))
         continue;

      internal_kfree(h, ptr + tot, sub_block_size, allow_split, do_actual_free);
      tot += sub_block_size;
   }

   ASSERT(tot == size);
}

/* Natural continuation of this source file. Purpose: make this file shorter. */
#include "kmalloc_small_heap.c.h"
#include "kmalloc_heaps.c.h"
#include "general_kmalloc.c.h"
#include "kmalloc_heaps_debug.c.h"
