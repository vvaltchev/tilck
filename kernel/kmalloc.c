
#include <kmalloc.h>
#include <paging.h>
#include <string_util.h>
#include <utils.h>

// MIN_BLOCK_SIZE has to be a multiple of 32
STATIC_ASSERT((MIN_BLOCK_SIZE & 31) == 0);

// HEAP_SIZE has to be a multiple of 1 MB
STATIC_ASSERT((HEAP_SIZE & ((1 << 20) - 1)) == 0);

// ALLOC_BLOCK_SIZE has to be a multiple of PAGE_SIZE
STATIC_ASSERT((ALLOC_BLOCK_SIZE & (PAGE_SIZE - 1)) == 0);

bool kbasic_virtual_alloc(uptr vaddr, int page_count);
void kbasic_virtual_free(uptr vaddr, int page_count);

#include "kmalloc_debug.h"

typedef struct {

   // 1 if the block has been split. Check its children.
   u8 split : 1;

   // 1 means obviously full
   // 0 means completely empty if split = 0, or partially empty if split = 1
   u8 full : 1;

   u8 allocated : 1; // used only for nodes having size = ALLOC_BLOCK_SIZE.

   u8 unused : 5;

} block_node;

STATIC_ASSERT(sizeof(block_node) == KMALLOC_METADATA_BLOCK_NODE_SIZE);

static inline block_node *get_allocator_metadata_nodes(void)
{
   return (block_node *)HEAP_BASE_ADDR;
}

static const block_node new_node; // Just zeros.

static int heap_data_size_log2;
static int alloc_block_size_log2;

bool kmalloc_initialized; // Zero-initialized => false.


#define HALF(x) ((x) >> 1)
#define TWICE(x) ((x) << 1)

#define NODE_LEFT(n) (TWICE(n) + 1)
#define NODE_RIGHT(n) (TWICE(n) + 2)
#define NODE_PARENT(n) (HALF(n-1))
#define NODE_IS_LEFT(n) (((n) & 1) != 0)

CONSTEXPR int ptr_to_node(void *ptr, size_t size)
{
   const int sizeLog = log2_for_power_of_2(size);

   const uptr raddr = (uptr)ptr - HEAP_DATA_ADDR;
   const int nodes_before_our = (1 << (heap_data_size_log2 - sizeLog)) - 1;
   const int position_in_row = raddr >> sizeLog;

   return nodes_before_our + position_in_row;
}

CONSTEXPR void *node_to_ptr(int node, size_t size)
{
   const int sizeLog = log2_for_power_of_2(size);

   const int nodes_before_our = (1 << (heap_data_size_log2 - sizeLog)) - 1;
   const int position_in_row = node - nodes_before_our;
   const uptr raddr = position_in_row << sizeLog;

   return (void *)(raddr + HEAP_DATA_ADDR);
}

CONSTEXPR static ALWAYS_INLINE bool is_block_node_free(block_node n)
{
   return !n.full && !n.split;
}

static size_t set_free_uplevels(int *node, size_t size)
{
   block_node *nodes = get_allocator_metadata_nodes();

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

static void *actual_allocate_node(size_t node_size, int node)
{
   block_node *nodes = get_allocator_metadata_nodes();
   nodes[node].full = true;

   const uptr vaddr = (uptr)node_to_ptr(node, node_size);

   uptr alloc_block_vaddr = vaddr & ~(ALLOC_BLOCK_SIZE - 1);
   const int alloc_block_count = 1 + ((node_size - 1) >> alloc_block_size_log2);

   const uptr alloc_block_over_end =
      alloc_block_vaddr + (alloc_block_count * ALLOC_BLOCK_SIZE);

   if (alloc_block_over_end <= KERNEL_LINEAR_MAPPING_OVER_END)
      return (void *)vaddr; // nothing to do!

   // DISALLOW allocations crossing the linear mapping barrier!!
   ASSERT(alloc_block_vaddr >= KERNEL_LINEAR_MAPPING_OVER_END);

   /*
    * Code dealing with the tricky allocation logic.
    */

   for (int i = 0; i < alloc_block_count; i++) {

      int alloc_node = ptr_to_node((void *)alloc_block_vaddr, ALLOC_BLOCK_SIZE);
      ASSERT(node_to_ptr(alloc_node,
                         ALLOC_BLOCK_SIZE) == (void *)alloc_block_vaddr);

      DEBUG_allocate_node1;

      if (!nodes[alloc_node].allocated) {

         DEBUG_allocate_node2;

         // TODO: handle out-of-memory
         DEBUG_ONLY(bool success =)
            kbasic_virtual_alloc(alloc_block_vaddr, ALLOC_BLOCK_PAGES);
         ASSERT(success);

         nodes[alloc_node].allocated = true;
      }

      if (node_size >= ALLOC_BLOCK_SIZE) {
         ASSERT(!nodes[alloc_node].split);
         nodes[alloc_node].full = true;
      } else {
         ASSERT(nodes[alloc_node].split);
      }

      alloc_block_vaddr += ALLOC_BLOCK_SIZE;
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

void *kmalloc(size_t desired_size)
{
   ASSERT(kmalloc_initialized);
   ASSERT(desired_size != 0);

   /*
    * ASSERTs that HEAP_BASE_ADDR is aligned at ALLOC_BLOCK_SIZE.
    * Without that condition the "magic" of ptr_to_node() and node_to_ptr()
    * does not work.
    */
   ASSERT((HEAP_BASE_ADDR & (ALLOC_BLOCK_SIZE - 1)) == 0);

   DEBUG_kmalloc_begin;

   if (UNLIKELY(desired_size > HEAP_SIZE))
      return NULL;

   const size_t size = MAX(desired_size, MIN_BLOCK_SIZE);
   block_node *nodes = get_allocator_metadata_nodes();

   int stack_size = 1;
   bool returned = false;
   stack_elem alloc_stack[32];

   stack_elem base_elem = { HEAP_SIZE, 0 };
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

         void *vaddr = actual_allocate_node(node_size, node);

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


void kfree(void *ptr, size_t size)
{
   ASSERT(kmalloc_initialized);

   if (ptr == NULL)
      return;

   // ptr == NULL with size == 0 is fine.
   ASSERT(size != 0);

   size = roundup_next_power_of_2(MAX(size, MIN_BLOCK_SIZE));
   const int node = ptr_to_node(ptr, size);

   DEBUG_free1;
   ASSERT(node_to_ptr(node, size) == ptr);

   block_node *nodes = get_allocator_metadata_nodes();

   // A node returned to user cannot be split.
   ASSERT(!nodes[node].split);


   // Walking up to mark the parent nodes as 'not full' if necessary..

   {
      int biggest_free_node = node;
      size_t biggest_free_size = set_free_uplevels(&biggest_free_node, size);

      DEBUG_free_after_coaleshe;

      ASSERT(biggest_free_node == node || biggest_free_size != size);

      if (biggest_free_size < ALLOC_BLOCK_SIZE)
         return;
   }

   uptr alloc_block_vaddr = (uptr)ptr & ~(ALLOC_BLOCK_SIZE - 1);
   const int alloc_block_count = 1 + ((size - 1) >> alloc_block_size_log2);

   const uptr alloc_block_over_end =
      alloc_block_vaddr + (alloc_block_count * ALLOC_BLOCK_SIZE);

   if (alloc_block_over_end <= KERNEL_LINEAR_MAPPING_OVER_END)
      return; // nothing to do!

   // DISALLOW allocations crossing the linear mapping barrier!!
   ASSERT(alloc_block_vaddr >= KERNEL_LINEAR_MAPPING_OVER_END);

   /*
    * Code dealing with the tricky allocation logic.
    */

   DEBUG_free_alloc_block_count;

   for (int i = 0; i < alloc_block_count; i++) {

      const int alloc_node =
         ptr_to_node((void *)alloc_block_vaddr, ALLOC_BLOCK_SIZE);

      DEBUG_check_alloc_block;

      /*
       * For nodes smaller than ALLOC_BLOCK_SIZE, the page we're freeing MUST
       * be free. For bigger nodes that kind of checking does not make sense:
       * a major block owns its all pages and their flags are irrelevant.
       */
      ASSERT(size >= ALLOC_BLOCK_SIZE ||
             is_block_node_free(nodes[alloc_node]));

      ASSERT(nodes[alloc_node].allocated);

      DEBUG_free_freeing_block;
      kbasic_virtual_free(alloc_block_vaddr, ALLOC_BLOCK_PAGES);

      nodes[alloc_node] = new_node;
      alloc_block_vaddr += ALLOC_BLOCK_SIZE;
   }
}


void initialize_kmalloc()
{
   ASSERT(!kmalloc_initialized);

   DEBUG_printk("heap base addr: %p\n", HEAP_BASE_ADDR);
   DEBUG_printk("heap data addr: %p\n", HEAP_DATA_ADDR);
   DEBUG_printk("heap size: %u\n", HEAP_SIZE);

   heap_data_size_log2 = log2_for_power_of_2(HEAP_SIZE);
   alloc_block_size_log2 = log2_for_power_of_2(ALLOC_BLOCK_SIZE);
   kmalloc_initialized = true;
}

