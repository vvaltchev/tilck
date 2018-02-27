
#include <kmalloc.h>
#include <paging.h>
#include <string_util.h>
#include <utils.h>

bool kbasic_virtual_alloc(uptr vaddr, int page_count);
void kbasic_virtual_free(uptr vaddr, int page_count);

#include "kmalloc_debug.h"

typedef struct {

   // 1 if the block has been split. Check its children.
   u8 split : 1;

   // 1 means obviously full
   // 0 means completely empty if split = 0, or partially empty if split = 1
   u8 full : 1;

   u8 allocated : 1; // used only for nodes having size = alloc_block_size.

   u8 unused : 5;

} block_node;

STATIC_ASSERT(sizeof(block_node) == KMALLOC_METADATA_BLOCK_NODE_SIZE);

static const block_node new_node; // Just zeros.
bool kmalloc_initialized; // Zero-initialized => false.


#define HALF(x) ((x) >> 1)
#define TWICE(x) ((x) << 1)

#define NODE_LEFT(n) (TWICE(n) + 1)
#define NODE_RIGHT(n) (TWICE(n) + 2)
#define NODE_PARENT(n) (HALF(n-1))
#define NODE_IS_LEFT(n) (((n) & 1) != 0)

static inline int ptr_to_node(kmalloc_heap *h, void *ptr, size_t size)
{
   const int size_log = log2_for_power_of_2(size);

   const uptr offset = (uptr)ptr - h->vaddr;
   const int nodes_before_our = (1 << (h->heap_data_size_log2 - size_log)) - 1;
   const int position_in_row = offset >> size_log;

   return nodes_before_our + position_in_row;
}

static inline void *node_to_ptr(kmalloc_heap *h, int node, size_t size)
{
   const int size_log = log2_for_power_of_2(size);

   const int nodes_before_our = (1 << (h->heap_data_size_log2 - size_log)) - 1;
   const int position_in_row = node - nodes_before_our;
   const uptr offset = position_in_row << size_log;

   return (void *)(offset + h->vaddr);
}

CONSTEXPR static ALWAYS_INLINE bool is_block_node_free(block_node n)
{
   return !n.full && !n.split;
}

static size_t set_free_uplevels(kmalloc_heap *h, int *node, size_t size)
{
   block_node *nodes = h->metadata_nodes;

   size_t curr_size = size << 1;
   int n = *node;

   nodes[n].full = false;
   n = NODE_PARENT(n);

   do {

      if (is_block_node_free(nodes[n])) {
         break;
      }

      block_node left = nodes[NODE_LEFT(n)];
      block_node right = nodes[NODE_RIGHT(n)];

      if (!is_block_node_free(left) || !is_block_node_free(right)) {
         DEBUG_stop_coaleshe;
         curr_size >>= 1;
         break;
      }

      *node = n; // last successful coaleshe.

      DEBUG_coaleshe;
      nodes[n].full = false;
      nodes[n].split = false;

      n = NODE_PARENT(n);
      curr_size <<= 1;

   } while (n != 0);

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

         // TODO: handle out-of-memory
         DEBUG_ONLY(bool success =)
            kbasic_virtual_alloc(alloc_block_vaddr,
                                 h->alloc_block_size / PAGE_SIZE);
         ASSERT(success);

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

//////////////////////////////////////////////////////////////////

typedef struct {

   size_t node_size;
   int node;

} stack_elem;


#define SIMULATE_CALL(a1, a2)                  \
   {                                           \
      stack_elem _elem_ = {(a1), (a2)};        \
      alloc_stack[stack_size++] = _elem_;      \
      continue;                                \
   }

#define SIMULATE_RETURN_NULL()                 \
   {                                           \
      stack_size--;                            \
      returned = true;                         \
      continue;                                \
   }

//////////////////////////////////////////////////////////////////

static void *internal_kmalloc(kmalloc_heap *h, size_t desired_size)
{
   ASSERT(kmalloc_initialized);
   ASSERT(desired_size != 0);

   /*
    * ASSERTs that metadata_nodes is aligned at h->alloc_block_size.
    * Without that condition the "magic" of ptr_to_node() and node_to_ptr()
    * does not work.
    */
   ASSERT(((uptr)h->metadata_nodes & (h->alloc_block_size - 1)) == 0);

   DEBUG_kmalloc_begin;

   if (UNLIKELY(desired_size > h->size))
      return NULL;

   const size_t size = MAX(desired_size, h->min_block_size);
   block_node *nodes = h->metadata_nodes;

   int stack_size = 1;
   bool returned = false;
   stack_elem alloc_stack[32];

   stack_elem base_elem = { h->size, 0 };
   alloc_stack[0] = base_elem;

   while (stack_size) {

      // Load the "stack" (function arguments)
      const size_t node_size = alloc_stack[stack_size - 1].node_size;
      const int node = alloc_stack[stack_size - 1].node;

      const size_t half_node_size = HALF(node_size);
      const int left_node = NODE_LEFT(node);
      const int right_node = NODE_RIGHT(node);

      // Handle a RETURN

      if (returned) {

         returned = false;

         if (alloc_stack[stack_size].node == left_node)
            goto after_left_call;
         else
            goto after_right_call;
      }

      // Handling a CALL
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

         // Walking up to mark the parent nodes as 'full' if necessary..

         for (int ss = stack_size - 2; ss > 0; ss--) {

            const int n = alloc_stack[ss].node;

            if (nodes[NODE_LEFT(n)].full && nodes[NODE_RIGHT(n)].full)
               nodes[n].full = true;
         }

         return vaddr;
      }


      if (!n.split) {
         DEBUG_kmalloc_split;

         nodes[node].split = true;

         nodes[left_node].split = false;
         nodes[left_node].full = false;

         nodes[right_node].split = false;
         nodes[right_node].full = false;
      }

      if (!nodes[left_node].full) {
         DEBUG_going_left;

         SIMULATE_CALL(half_node_size, left_node);

         after_left_call:
         // The call on the left node returned NULL so, go to the right node.

         DEBUG_left_failed;
         SIMULATE_CALL(half_node_size, right_node);

      } else if (!nodes[right_node].full) {
         DEBUG_going_right;

         SIMULATE_CALL(half_node_size, right_node);

         after_right_call:
         SIMULATE_RETURN_NULL();
      }

      // In case nor the left nor the right child is free, just return NULL.
      SIMULATE_RETURN_NULL();
   }

   return NULL;
}

#undef SIMULATE_CALL
#undef SIMULATE_RETURN_NULL

static void internal_kfree(kmalloc_heap *h, void *ptr, size_t size)
{
   ASSERT(kmalloc_initialized);

   if (ptr == NULL)
      return;

   // NOTE: ptr == NULL with size == 0 is fine.
   ASSERT(size != 0);

   size = roundup_next_power_of_2(MAX(size, h->min_block_size));
   const int node = ptr_to_node(h, ptr, size);

   DEBUG_free1;
   ASSERT(node_to_ptr(h, node, size) == ptr);

   block_node *nodes = h->metadata_nodes;

   // A node returned to user cannot be split.
   ASSERT(!nodes[node].split);


   // Walking up to mark the parent nodes as 'not full' if necessary..

   {
      int biggest_free_node = node;
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
      kbasic_virtual_free(alloc_block_vaddr, h->alloc_block_size / PAGE_SIZE);

      nodes[alloc_node] = new_node;
      alloc_block_vaddr += h->alloc_block_size;
   }
}

static kmalloc_heap heaps[8];

void *kmalloc(size_t s)
{
   // Iterate in reverse-order because the first heaps are the biggest ones.
   for (int i = ARRAY_SIZE(heaps) - 1; i >= 0; i--) {

      /*
       * The heap is too small (unlikely but possible) or the heap has not been
       * created yet, therefore has size = 0.
       */
      if (heaps[i].size < s)
         continue;

      void *vaddr = internal_kmalloc(&heaps[i], s);

      if (vaddr)
         return vaddr;
   }

   return NULL;
}

void kfree(void *p, size_t s)
{
   const uptr vaddr = (uptr) p;

   for (int i = ARRAY_SIZE(heaps) - 1; i >= 0; i--) {

      if (heaps[i].size < s)
         continue;

      if (vaddr >= heaps[i].vaddr && vaddr + s <= heaps[i].heap_over_end) {
         internal_kfree(&heaps[i], p, s);
         return;
      }
   }

   panic("[kfree] Heap not found for block: %p of size: %u", p, s);
}

void kmalloc_create_heap(kmalloc_heap *h,
                         uptr vaddr,
                         size_t size,
                         size_t min_block_size,
                         size_t alloc_block_size,
                         void *metadata_nodes)
{
   // vaddr has to be MB-aligned
   ASSERT((vaddr & (MB - 1)) == 0);

   // heap size has to be a multiple of 1 MB
   ASSERT((size & (MB - 1)) == 0);

   // alloc block size has to be a multiple of PAGE_SIZE
   ASSERT((alloc_block_size & (PAGE_SIZE - 1)) == 0);

   bzero(h, sizeof(*h));

   if (!metadata_nodes) {
      // It is OK to pass NULL as 'metadata_nodes' if at least one heap exists.
      ASSERT(heaps[0].vaddr != 0);

      metadata_nodes =
         kmalloc(calculate_heap_metadata_size(size, min_block_size));

      VERIFY(metadata_nodes != NULL);
   }

   h->vaddr = vaddr;
   h->size = size;
   h->min_block_size = min_block_size;
   h->alloc_block_size = alloc_block_size;
   h->metadata_nodes = metadata_nodes;

   h->heap_over_end = vaddr + size;
   h->heap_data_size_log2 = log2_for_power_of_2(size);
   h->alloc_block_size_log2 = log2_for_power_of_2(alloc_block_size);

   bzero(h->metadata_nodes, calculate_heap_metadata_size(size, min_block_size));

   if (vaddr + size <= LINEAR_MAPPING_OVER_END) {
      h->linear_mapping = true;
   } else {
      // DISALLOW heaps crossing the linear mapping barrier.
      ASSERT(vaddr >= LINEAR_MAPPING_OVER_END);
   }
}

static size_t find_biggest_heap_size(uptr vaddr, uptr limit)
{
   size_t curr_max = 512 * MB;

   while (curr_max) {

      if (vaddr + curr_max <= limit)
         break;

      curr_max >>= 1; // divide by 2.
   }

   return curr_max;
}

void initialize_kmalloc()
{
   ASSERT(!kmalloc_initialized);

   const uptr limit =
      KERNEL_BASE_VA + MIN(get_phys_mem_mb(), LINEAR_MAPPING_MB) * MB;

   uptr vaddr = KERNEL_BASE_VA + (INITIAL_MB_RESERVED +
                                  MB_RESERVED_FOR_PAGING +
                                  RAMDISK_MB) * MB;

   const size_t fixed_size_first_heap_metadata = 1 * MB;

   const size_t first_heap_size =
      find_biggest_heap_size(vaddr + fixed_size_first_heap_metadata, limit);

   size_t min_block_size =
      sizeof(block_node) * 2 * first_heap_size / fixed_size_first_heap_metadata;

#ifndef UNIT_TEST_ENVIRONMENT
   printk("[kmalloc] First heap size: %u MB, min block: %u\n",
          first_heap_size / MB, min_block_size);
#endif

   ASSERT(calculate_heap_metadata_size(
            first_heap_size, min_block_size) == fixed_size_first_heap_metadata);

   kmalloc_create_heap(&heaps[0],
                       vaddr + fixed_size_first_heap_metadata,
                       first_heap_size,
                       min_block_size,
                       32 * PAGE_SIZE,
                       (void *)vaddr);

   kmalloc_initialized = true;
   vaddr = heaps[0].vaddr + heaps[0].size;

   for (size_t i = 1; i < ARRAY_SIZE(heaps); i++) {

      const size_t heap_size = find_biggest_heap_size(vaddr, limit);

      if (heap_size < MB)
         break;

      min_block_size = heap_size / (256 * KB);

#ifndef UNIT_TEST_ENVIRONMENT
      printk("[kmalloc] heap size: %u MB, min block: %u\n",
             heap_size / MB, min_block_size);
#endif

      kmalloc_create_heap(&heaps[i],
                          vaddr,
                          heap_size,
                          min_block_size,
                          32 * PAGE_SIZE,
                          NULL);

      vaddr = heaps[i].vaddr + heaps[i].size;
   }
}

