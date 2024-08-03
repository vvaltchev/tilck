/* SPDX-License-Identifier: BSD-2-Clause */

#define _KMALLOC_C_

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/kmalloc_debug.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/sort.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/worker_thread.h>

#include <tilck_gen_headers/config_kmalloc.h>

#include "kmalloc_debug.h"
#include "kmalloc_heap_struct.h"
#include "kmalloc_block_node.h"

size_t kmalloc_get_heap_struct_size(void)
{
   return sizeof(struct kmalloc_heap);
}

STATIC_ASSERT(sizeof(struct block_node) == KMALLOC_METADATA_BLOCK_NODE_SIZE);

STATIC bool kmalloc_initialized;
static const struct block_node s_new_node; // Just zeros.

#define HALF(x) ((x) >> 1)
#define TWICE(x) ((x) << 1)

#define NODE_LEFT(n) (TWICE(n) + 1)
#define NODE_RIGHT(n) (TWICE(n) + 2)
#define NODE_PARENT(n) (HALF(n-1))
#define NODE_IS_LEFT(n) (((n) & 1) != 0)

static void
per_heap_kfree_unsafe(struct kmalloc_heap *h,
                      void *ptr,
                      size_t *user_size,
                      u32 flags);

static void
internal_kfree(struct kmalloc_heap *h,
               void *ptr,
               size_t size,
               bool allow_split,
               bool do_actual_free);

bool is_kmalloc_initialized(void)
{
   return kmalloc_initialized;
}

STATIC_INLINE int ptr_to_node(struct kmalloc_heap *h, void *ptr, size_t size)
{
   const ulong size_log = log2_for_power_of_2(size);

   const ulong offset = (ulong)ptr - h->vaddr;
   const ulong nodes_before_our = (1 << (h->heap_data_size_log2 - size_log))-1;
   const ulong position_in_row = offset >> size_log;

   return (int)(nodes_before_our + position_in_row);
}

STATIC_INLINE void *node_to_ptr(struct kmalloc_heap *h, int node, size_t size)
{
   const ulong size_log = log2_for_power_of_2(size);

   const ulong nodes_before_our = (1 << (h->heap_data_size_log2 - size_log))-1;
   const ulong position_in_row = (u32)node - nodes_before_our;
   const ulong offset = position_in_row << size_log;

   return (void *)(offset + h->vaddr);
}

CONSTEXPR static ALWAYS_INLINE bool is_block_node_free(struct block_node n)
{
   return !(n.raw & (FL_NODE_FULL | FL_NODE_SPLIT));
}

static size_t set_free_uplevels(struct kmalloc_heap *h, int *node, size_t size)
{
   struct block_node *nodes = h->metadata_nodes;

   size_t curr_size = size << 1;
   int n = *node;

   ASSERT(!nodes[n].split);

   nodes[n].full = false;
   n = NODE_PARENT(n);

   while (!is_block_node_free(nodes[n])) {

      struct block_node left = nodes[NODE_LEFT(n)];
      struct block_node right = nodes[NODE_RIGHT(n)];

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
actual_allocate_node(struct kmalloc_heap *h,
                     size_t node_size,
                     int node,
                     void **vaddr_ref,
                     bool do_actual_alloc)
{
   bool alloc_failed = false;
   struct block_node *nodes = h->metadata_nodes;
   nodes[node].full = true;

   const ulong vaddr = (ulong)node_to_ptr(h, node, node_size);
   *vaddr_ref = (void *)vaddr;

   if (h->linear_mapping || !do_actual_alloc)
      return true; // nothing to do!

   ulong alloc_block_vaddr = vaddr & ~(h->alloc_block_size - 1);
   const size_t alloc_block_count =
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

static size_t calculate_node_size(struct kmalloc_heap *h, int node)
{
   size_t size = h->size;

   for (int cn = node; cn != 0; cn = NODE_PARENT(cn)) {
      size >>= 1;
   }

   return size;
}

void
internal_kmalloc_split_block(struct kmalloc_heap *h,
                             void *const vaddr,
                             const size_t block_size,
                             const size_t leaf_node_size)
{
   ASSERT(leaf_node_size >= h->min_block_size);
   ASSERT(block_size >= h->min_block_size);
   ASSERT(roundup_next_power_of_2(leaf_node_size) == leaf_node_size);
   ASSERT(roundup_next_power_of_2(block_size) == block_size);

   struct block_node *nodes = h->metadata_nodes;

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
internal_kmalloc_coalesce_block(struct kmalloc_heap *h,
                                void *const vaddr,
                                const size_t block_size)
{
   struct block_node *nodes = h->metadata_nodes;
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
internal_kmalloc(struct kmalloc_heap *h,
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

   struct block_node *nodes = h->metadata_nodes;
   int stack_size = 0;

   if (!start_node_size)
      start_node_size = calculate_node_size(h, start_node);

   SIMULATE_CALL2(start_node_size, start_node);

   NOREC_LOOP_BEGIN
   {
      // Load the "stack" (function arguments)
      const size_t node_size = LOAD_ARG_FROM_STACK(1, size_t);
      const int node = LOAD_ARG_FROM_STACK(2, int);

      HANDLE_SIMULATED_RETURN();

      // Handle a SIMULATED "call"
      DEBUG_kmalloc_call_begin;

      struct block_node n = nodes[node];

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

            const int nn = (int)(long) STACK_VAR[ss].arg2; /* arg2: node */

            if (nodes[NODE_LEFT(nn)].full && nodes[NODE_RIGHT(nn)].full) {
               ASSERT(!nodes[nn].full);
               nodes[nn].full = true;
            }
         }

         if (UNLIKELY(!success)) {

            /*
             * Corner case: in case of non-linearly mapped heaps, a successful
             * allocation in the heap metadata does not always mean a successful
             * kmalloc(), because the underlying allocator [h->valloc_and_map]
             * might have failed. In this case we have to call per_heap_kfree
             * and restore kmalloc's heap metadata to the previous state. Also,
             * all the alloc nodes should be either marked as allocated or
             * alloc_failed.
             */

            DEBUG_kmalloc_bad_end;
            size_t actual_size = node_size;
            per_heap_kfree_unsafe(h, vaddr, &actual_size, 0);
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
   }
   NOREC_LOOP_END
   return NULL;
}

static void *
per_heap_kmalloc_unsafe(struct kmalloc_heap *h, size_t *size, u32 flags)
{
   void *addr;
   const bool multi_step_alloc = !!(flags & KMALLOC_FL_MULTI_STEP);
   const bool do_actual_alloc = !(flags & KMALLOC_FL_NO_ACTUAL_ALLOC);
   const u32 sub_blocks_min_size = flags & KMALLOC_FL_SUB_BLOCK_MIN_SIZE_MASK;
   const bool do_split = (sub_blocks_min_size != 0);
   const size_t original_desired_size = *size;

   ASSERT(original_desired_size != 0);
   ASSERT(!do_split || sub_blocks_min_size >= h->min_block_size);
   ASSERT(!is_preemption_enabled());

   DEBUG_kmalloc_begin;

   if (UNLIKELY(original_desired_size > h->size)) {
      /* The requested memory chunk is bigger than the heap. */
      return NULL;
   }

   /*
    * Round-up `original_desired_size` to the minimal block size for the heap.
    */
   const size_t rounded_up_size =
      MAX(roundup_next_power_of_2(original_desired_size), h->min_block_size);

   if (!multi_step_alloc ||
       ((rounded_up_size - original_desired_size) < h->min_block_size))
   {
      /*
       * This is not a multi-step allocation, or it is but the rounded-up size
       * is closer to the desired chunk size than our smallest block.
       */
      *size = rounded_up_size;
      addr = internal_kmalloc(h,          /* heap */
                              *size,      /* block size */
                              0,          /* start node */
                              h->size,    /* start node size */
                              true,       /* mark node as allocated */
                              do_actual_alloc);

      if (do_split && addr) {
         internal_kmalloc_split_block(h, addr, *size, sub_blocks_min_size);
      }
      return addr;
   }

   /*
    * multi_step_alloc is true, therefore we can do multiple allocations in
    * order to allocate almost exactly original_desired_size bytes, with a
    * waste smaller than `h->min_block_size`.
    *
    * NOTE: we don't have to mark as free the remaining of big_block after the
    * "for" loop below, because the first internal_kmalloc() below is called
    * with mark_node_as_allocated = false and do_actual_alloc = false. In other
    * words, the following call has no side effects. We just need to find
    * the biggest block that could contain our allocation.
    */

   const size_t desired_size = *size;
   void *big_block = internal_kmalloc(h,                /* heap */
                                      rounded_up_size,  /* chunk size */
                                      0,                /* start node */
                                      h->size,          /* start node size */
                                      false,            /* mark as allocated */
                                      false);           /* do actual alloc */

   if (!big_block)
      return NULL;

   const int big_block_node = ptr_to_node(h, big_block, rounded_up_size);
   const int power_of_two_start = (int)h->heap_data_size_log2 - 1;
   size_t tot = 0;

   /*
    * Allocate `desired_size`, one power-of-two at a time, starting from the
    * power immediately after the heap_size_log2. The allocations are guaranteed
    * to be contiguous because we already checked that the whole big_block is
    * available for us.
    */
   for (int i = power_of_two_start; i >= 0 && tot < desired_size; i--)
   {
      size_t sub_block_size = (1u << i);

      if (!(desired_size & sub_block_size)) {
         /*
          * desired_size (a regular not-power-of-two number) does not have the
          * N-th bit set, corresponding to `sub_block_size`. Therefore, we skip
          * allocating a sub-block of this size.
          */
         continue;
      }

      /*
       * Do one power-of-two allocation of size `sub_block_size`. Note that we
       * ALWAYS start from `big_block_node`, on each iteration. The internal
       * implementation will always gives us the next address. We ASSERT for
       * that below.
       */
      addr = internal_kmalloc(h,
                              sub_block_size,
                              big_block_node,
                              rounded_up_size,
                              true,              /* mark node as allocated */
                              do_actual_alloc);

      if (UNLIKELY(!addr)) {

         /*
          * The internal_kmalloc() above can fail only if `do_actual_alloc`
          * is set (and the underlying call to valloc_and_map() failed).
          */
         ASSERT(do_actual_alloc);

         /*
          * We need to roll-back the whole thing. Start from the last value of
          * `i` that might have been used and end only when `tot` becomes 0.
          */
         for  (int j = i + 1; tot > 0; j++) {

            ASSERT(j <= power_of_two_start);

            sub_block_size = (1u << j);
            if (!(desired_size & sub_block_size)) {
               /* sub_block_size not used, see the longer comment above. */
               continue;
            }

            /*
             * To calculate backwards the pointer to the last sub-block,
             * we must first subtract its size from `tot`, because the last
             * block is within the following range:
             *    [big_block + tot - sub_block_size, big_block + tot).
             */
            tot -= sub_block_size;
            internal_kfree(h,
                           big_block + tot,
                           sub_block_size,
                           do_split,            /* allow split */
                           do_actual_alloc);
         }
         return NULL;
      }

      /*
       * Make absolutely sure that the address we got is immediately
       * adjacent to the last allocated sub-block.
       */
      ASSERT(addr == big_block + tot);

      if (do_split) {
         internal_kmalloc_split_block(h,
                                      addr,
                                      sub_block_size,
                                      sub_blocks_min_size);
      }

      tot += sub_block_size;
   }

   ASSERT(tot >= original_desired_size);
   *size = tot;
   return big_block;
}


void *
per_heap_kmalloc(struct kmalloc_heap *h, size_t *size, u32 flags)
{
   bool expected = false;
   void *res;

   if (!atomic_cas_strong(&h->in_use, &expected, true, mo_relaxed, mo_relaxed))
      return NULL; /* heap already in use (we're in IRQ context) */

   res = per_heap_kmalloc_unsafe(h, size, flags);
   atomic_store_explicit(&h->in_use, false, mo_relaxed);
   return res;
}

static void
internal_kfree(struct kmalloc_heap *h,
               void *ptr,
               size_t size,
               bool allow_split,
               bool do_actual_free)
{
   ASSERT(size);

   const int node = ptr_to_node(h, ptr, size);
   size_t free_size_correction = 0;

   DEBUG_free1;
   ASSERT(node_to_ptr(h, node, size) == ptr);
   ASSERT(roundup_next_power_of_2(size) == size);
   ASSERT(size >= h->min_block_size);

   struct block_node *nodes = h->metadata_nodes;

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

   ulong alloc_block_vaddr = (ulong)ptr & ~(h->alloc_block_size - 1);
   const size_t alloc_block_count = 1 + ((size-1) >> h->alloc_block_size_log2);

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

static size_t calculate_block_size(struct kmalloc_heap *h, ulong vaddr)
{
   struct block_node *nodes = h->metadata_nodes;
   int n = 0; /* root's node index */
   ulong va = h->vaddr; /* root's node data address == heap's address */
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
debug_check_block_size(struct kmalloc_heap *h, ulong vaddr, size_t size)
{
   size_t cs = calculate_block_size(h, vaddr);

   if (cs != size) {
      panic("calculated_size[%u] != user_size[%u] for block at: %p. "
            "Double free?", cs, size, vaddr);
   }
}

static void
per_heap_kfree_unsafe(struct kmalloc_heap *h,
                      void *ptr,
                      size_t *user_size,
                      u32 flags)
{
   size_t size;
   ulong vaddr = (ulong)ptr;

   const bool allow_split = !!(flags & KFREE_FL_ALLOW_SPLIT);
   const bool multi_step_free = !!(flags & KFREE_FL_MULTI_STEP);
   const bool do_actual_free = !(flags & KFREE_FL_NO_ACTUAL_FREE);

   ASSERT(!is_preemption_enabled());
   ASSERT(vaddr >= h->vaddr);
   ASSERT(ptr);

   if (!multi_step_free) {

      if (*user_size) {

         size = roundup_next_power_of_2(MAX(*user_size, h->min_block_size));

         if (!allow_split) {
            DEBUG_ONLY(debug_check_block_size(h, vaddr, size));
         }

      } else {

         ASSERT(!allow_split);
         size = calculate_block_size(h, vaddr);
      }

      *user_size = size;
      ASSERT(vaddr + size - 1 <= h->heap_last_byte);
      return internal_kfree(h, ptr, size, allow_split, do_actual_free);
   }

   ASSERT(*user_size);
   size = *user_size; /*
                       * No round-up: when multi_step_free=1 we assume that the
                       * size is the one returned as an out-param by
                       * per_heap_kmalloc(). Also, we don't have to alter
                       * *user_size too.
                       */

   ASSERT(vaddr + size - 1 <= h->heap_last_byte);
   ASSERT(pow2_round_up_at(size, h->min_block_size) == size);

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

struct deferred_kfree_ctx {

   struct kmalloc_heap *h;
   void *ptr;
   size_t user_size;
   u32 flags;
};

static struct deferred_kfree_ctx emergency_deferred_kfree_ctx_array[8];

static struct deferred_kfree_ctx *
get_emergency_deferred_kfree_ctx(void *ptr)
{
   for (int i = 0; i < ARRAY_SIZE(emergency_deferred_kfree_ctx_array); i++) {
      if (emergency_deferred_kfree_ctx_array[i].ptr == NULL) {
         emergency_deferred_kfree_ctx_array[i].ptr = ptr;
         return &emergency_deferred_kfree_ctx_array[i];
      }
   }

   return NULL;
}

static bool
is_emerg_deferred_kfree_ctx(struct deferred_kfree_ctx *ctx)
{
   return IN_RANGE(
      (ulong)ctx,
      (ulong)&emergency_deferred_kfree_ctx_array[0],
      (ulong)&emergency_deferred_kfree_ctx_array[0]
         + sizeof(emergency_deferred_kfree_ctx_array)
   );
}

static void
do_deferred_kfree(void *arg)
{
   struct deferred_kfree_ctx *ctx = arg;

   disable_preemption();
   {
      per_heap_kfree(ctx->h, ctx->ptr, &ctx->user_size, ctx->flags);
      bzero(ctx, sizeof(*ctx));
   }
   enable_preemption_nosched();

   if (!is_emerg_deferred_kfree_ctx(ctx))
      kfree_obj(ctx, struct deferred_kfree_ctx);
}

NO_INLINE static void
per_heap_kfree_used_heap_corner_case(struct kmalloc_heap *h,
                                     void *ptr,
                                     size_t *size,
                                     u32 flags)
{
   const bool multi_step_free = !!(flags & KFREE_FL_MULTI_STEP);

   /*
    * Corner case: probably because of ACPICA, we need to free a
    * heap-allocated object in IRQ context WHILE the very same heap is
    * already in use. That's extremely unfortunate.
   */

   if (!multi_step_free) {

      if (*size) {
         *size = roundup_next_power_of_2(MAX(*size, h->min_block_size));
      } else {
         /* No size: sorry, cannot give you this information in this case */
      }

   } else {

      /* Here *size is REQUIRED */
      ASSERT(*size);
   }

   struct deferred_kfree_ctx *ctx = kalloc_obj(struct deferred_kfree_ctx);

   if (!ctx) {

      /* Could we get more unlucky than that? */
      ctx = get_emergency_deferred_kfree_ctx(ptr);

      if (!ctx) {
         /* Give up */
         printk("kmalloc: ERROR: can't defer kfree %p: OOM\n", ptr);
         return;
      }
   }

   *ctx = (struct deferred_kfree_ctx) {
      .h = h,
      .ptr = ptr,
      .user_size = *size,
      .flags = flags
   };

   if (!wth_enqueue_anywhere(WTH_PRIO_HIGHEST, &do_deferred_kfree, ctx))
      printk("kmalloc: ERROR: can't defer kfree %p: enqueue fail\n", ptr);
}

void
per_heap_kfree(struct kmalloc_heap *h,
               void *ptr,
               size_t *user_size,
               u32 flags)
{
   bool expected = false;

   if (!ptr)
      return;

   if (!atomic_cas_strong(&h->in_use, &expected, true, mo_relaxed, mo_relaxed))
   {
      per_heap_kfree_used_heap_corner_case(h, ptr, user_size, flags);
      return;
   }

   per_heap_kfree_unsafe(h, ptr, user_size, flags);
   atomic_store_explicit(&h->in_use, false, mo_relaxed);
}

void *kzmalloc(size_t size)
{
   void *res = kmalloc(size);

   if (!res)
      return NULL;

   bzero(res, size);
   return res;
}

static
void vfree_internal(ulong va_begin, ulong va_end)
{
   for (ulong va = va_begin; va < va_end; va += PAGE_SIZE)
      unmap_kernel_page(TO_PTR(va), true);
}

void *vmalloc(size_t size)
{
   size_t actual_sz = pow2_round_up_at(size, PAGE_SIZE);
   ulong va, va_begin, va_end;
   void *ptr;

   if (!hi_vmem_avail())
      return kmalloc(size);

   ptr = kmalloc(size);

   if (ptr)
      return ptr;

   ptr = hi_vmem_reserve(actual_sz);

   va_begin = (ulong)ptr;
   va_end = va_begin + actual_sz;

   for (va = va_begin; va < va_end; va += PAGE_SIZE) {
      if (map_kernel_page(TO_PTR(va), 0, PAGING_FL_RW | PAGING_FL_DO_ALLOC))
         goto oom_case;
   }

   return TO_PTR(va_begin);

oom_case:

   va_end = va;
   vfree_internal(va_begin, va_end);
   hi_vmem_release(TO_PTR(va_begin), actual_sz);
   return NULL;

}

void vfree2(void *ptr, size_t size)
{
   if (!ptr)
      return;

   if ((ulong)ptr < LINEAR_MAPPING_END)
      return kfree2(ptr, size);

   size_t actual_sz = pow2_round_up_at(size, PAGE_SIZE);
   ulong va_begin, va_end;

   va_begin = (ulong)ptr;
   va_end = va_begin + actual_sz;

   vfree_internal(va_begin, va_end);
   hi_vmem_release(TO_PTR(va_begin), actual_sz);
}

/* Natural continuation of this source file. Purpose: make this file shorter. */
#include "kmalloc_stats.c.h"
#include "kmalloc_small_heaps.c.h"
#include "kmalloc_heaps.c.h"
#include "general_kmalloc.c.h"
#include "kmalloc_accelerator.c.h"

