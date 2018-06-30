
#define _KMALLOC_C_

#include <common/string_util.h>
#include <common/utils.h>

#include <exos/kmalloc.h>
#include <exos/paging.h>
#include <exos/sync.h>
#include <exos/process.h>
#include <exos/sort.h>

#include "kmalloc_debug.h"
#include "kmalloc_heap_struct.h"

size_t kmalloc_get_heap_struct_size(void)
{
   return sizeof(kmalloc_heap);
}

#define FL_NODE_SPLIT      (1 << 0)
#define FL_NODE_FULL       (1 << 1)
#define FL_NODE_ALLOCATED  (1 << 2)

typedef struct {

   union {

      struct {
         // 1 if the block has been split. Check its children.
         u8 split : 1;

         // 1 means obviously completely full
         // 0 means completely empty if split=0, or partially empty if split=1
         u8 full : 1;

         u8 allocated : 1; // used only for nodes having size = alloc_block_size

         u8 unused : 5; // Free unused (for now) bits.
      };

      u8 raw;
   };

} block_node;

STATIC_ASSERT(sizeof(block_node) == KMALLOC_METADATA_BLOCK_NODE_SIZE);

bool kmalloc_initialized; // Zero-initialized => false.
static const block_node new_node; // Just zeros.

#define HALF(x) ((x) >> 1)
#define TWICE(x) ((x) << 1)

#define NODE_LEFT(n) (TWICE(n) + 1)
#define NODE_RIGHT(n) (TWICE(n) + 2)
#define NODE_PARENT(n) (HALF(n-1))
#define NODE_IS_LEFT(n) (((n) & 1) != 0)

STATIC_INLINE int ptr_to_node(kmalloc_heap *h, void *ptr, size_t size)
{
   const int size_log = log2_for_power_of_2(size);

   const uptr offset = (uptr)ptr - h->vaddr;
   const int nodes_before_our = (1 << (h->heap_data_size_log2 - size_log)) - 1;
   const int position_in_row = offset >> size_log;

   return nodes_before_our + position_in_row;
}

STATIC_INLINE void *node_to_ptr(kmalloc_heap *h, int node, size_t size)
{
   const int size_log = log2_for_power_of_2(size);

   const int nodes_before_our = (1 << (h->heap_data_size_log2 - size_log)) - 1;
   const int position_in_row = node - nodes_before_our;
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

static void *actual_allocate_node(kmalloc_heap *h, size_t node_size, int node)
{
   block_node *nodes = h->metadata_nodes;
   nodes[node].full = true;

   const uptr vaddr = (uptr)node_to_ptr(h, node, node_size);

   if (h->linear_mapping)
      return (void *)vaddr; // nothing to do!

   uptr alloc_block_vaddr = vaddr & ~(h->alloc_block_size - 1);
   const int alloc_block_count =
      1 + ((node_size - 1) >> h->alloc_block_size_log2);

   /*
    * Code dealing with the tricky allocation logic.
    */

   for (int i = 0; i < alloc_block_count; i++) {

      int alloc_node = ptr_to_node(h,
                                   (void *)alloc_block_vaddr,
                                   h->alloc_block_size);

      ASSERT(node_to_ptr(h, alloc_node,
                         h->alloc_block_size) == (void *)alloc_block_vaddr);

      DEBUG_allocate_node1;

      if (!nodes[alloc_node].allocated) {

         DEBUG_allocate_node2;

         bool success =
            h->valloc_and_map(alloc_block_vaddr,
                              h->alloc_block_size / PAGE_SIZE);

         VERIFY(success); // TODO: handle out-of-memory
         nodes[alloc_node].allocated = true;
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
   return (void *)vaddr;
}


#define STACK_VAR alloc_stack
#include <common/norec.h>

/*
 * Explicit stack used by internal_kmalloc().
 * Keeping it as a global variable helps keeping the amount of stack used by
 * the kernel to be very small. Also, it does not create any problems since:
 *    - the kernel does not support SMP
 *    - the allocator is not reentrant, therefore kmalloc() can be called only
 *      in contexts where the kernel preemption is disabled.
 */
static struct explicit_stack_elem2 alloc_stack[32];

void *internal_kmalloc(kmalloc_heap *h, size_t desired_size)
{
   ASSERT(desired_size != 0);

   if (!h->linear_mapping) {

      /*
       * ASSERTs that metadata_nodes is aligned at h->alloc_block_size.
       * Without that condition the "magic" of ptr_to_node() and node_to_ptr()
       * does not work.
       */
      ASSERT(((uptr)h->metadata_nodes & (h->alloc_block_size - 1)) == 0);
   }

   DEBUG_kmalloc_begin;

   if (UNLIKELY(desired_size > h->size))
      return NULL;

   const size_t size = MAX(desired_size, h->min_block_size);
   block_node *nodes = h->metadata_nodes;

   int stack_size = 0;

   SIMULATE_CALL2(h->size /* node size */, 0 /* node number */);

   while (stack_size) {

      // Load the "stack" (function arguments)
      const size_t node_size = LOAD_ARG_FROM_STACK(1, size_t);
      const int node = LOAD_ARG_FROM_STACK(2, int);

      const size_t half_node_size = HALF(node_size);
      const int left_node = NODE_LEFT(node);
      const int right_node = NODE_RIGHT(node);

      HANDLE_SIMULATED_RETURN();

      // Handle a SIMULATED "call"
      DEBUG_kmalloc_call_begin;

      block_node n = nodes[node];

      if (n.full) {
         DEBUG_already_full;
         SIMULATE_RETURN_NULL();
      }

      if (half_node_size < size) {

         if (n.split) {
            DEBUG_already_split;
            SIMULATE_RETURN_NULL();
         }

         void *vaddr = actual_allocate_node(h, node_size, node);

         // Mark the parent nodes as 'full', when necessary.

         for (int ss = stack_size - 2; ss >= 0; ss--) {

            const int n = (uptr) alloc_stack[ss].arg2; /* arg2: node */

            if (nodes[NODE_LEFT(n)].full && nodes[NODE_RIGHT(n)].full) {
               ASSERT(!nodes[n].full);
               nodes[n].full = true;
            }
         }

         DEBUG_kmalloc_end;
         return vaddr;
      }

      if (!n.split) {
         DEBUG_kmalloc_split;

         nodes[node].split = true;
         nodes[left_node].raw &= ~(FL_NODE_SPLIT & FL_NODE_FULL);
         nodes[right_node].raw &= ~(FL_NODE_SPLIT & FL_NODE_FULL);
      }

      if (!nodes[left_node].full) {

         DEBUG_going_left;
         SIMULATE_CALL2(half_node_size, left_node);

         /*
          * If we got here, the "call" on the left node "returned" NULL so,
          * we have to try go to the right node. [In case of success, this
          * function returns directly.]
          */

         DEBUG_left_failed;
      }

      if (!nodes[right_node].full) {
         DEBUG_going_right;
         SIMULATE_CALL2(half_node_size, right_node);

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


void internal_kfree2(kmalloc_heap *h, void *ptr, size_t size)
{
   const int node = ptr_to_node(h, ptr, size);

   DEBUG_free1;
   ASSERT(node_to_ptr(h, node, size) == ptr);

   block_node *nodes = h->metadata_nodes;

   // A node returned to user cannot be split.
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

   if (h->linear_mapping)
      return; // nothing to do!

   uptr alloc_block_vaddr = (uptr)ptr & ~(h->alloc_block_size - 1);
   const int alloc_block_count = 1 + ((size - 1) >> h->alloc_block_size_log2);

   /*
    * Code dealing with the tricky allocation logic.
    */

   DEBUG_free_alloc_block_count;

   for (int i = 0; i < alloc_block_count; i++) {

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

      ASSERT(nodes[alloc_node].allocated);

      DEBUG_free_freeing_block;
      h->vfree_and_unmap(alloc_block_vaddr, h->alloc_block_size / PAGE_SIZE);

      nodes[alloc_node] = new_node;
      alloc_block_vaddr += h->alloc_block_size;
   }
}

/* Natural continuation of this source file. Purpose: make this file shorter. */
#include "kmalloc_heaps.c.h"
