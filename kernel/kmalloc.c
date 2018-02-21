
#include <kmalloc.h>
#include <paging.h>
#include <string_util.h>
#include <utils.h>

// MIN_BLOCK_SIZE has to be a multiple of 32
STATIC_ASSERT((MIN_BLOCK_SIZE & 31) == 0);

// HEAP_DATA_SIZE has to be a multiple of 1 MB
STATIC_ASSERT((HEAP_DATA_SIZE & ((1 << 20) - 1)) == 0);

// ALLOC_BLOCK_SIZE has to be a multiple of PAGE_SIZE
STATIC_ASSERT((ALLOC_BLOCK_SIZE & (PAGE_SIZE - 1)) == 0);

bool kbasic_virtual_alloc(uptr vaddr, int page_count);
bool kbasic_virtual_free(uptr vaddr, int page_count);

//#define DEBUG_printk printk
#define DEBUG_printk(...)

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

typedef struct {

   block_node nodes[KMALLOC_NODES_COUNT_IN_META_DATA];

} allocator_meta_data;


/*
 * Each byte represents 8 * PAGE_SIZE bytes = 32 KB.
 */

#define ALLOC_METADATA_SIZE \
   (sizeof(block_node) * KMALLOC_NODES_COUNT_IN_META_DATA / (8 * PAGE_SIZE))

static bool allocation_for_metadata_nodes[ALLOC_METADATA_SIZE];
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

static size_t set_free_uplevels(int *node, size_t size) {

   allocator_meta_data *md = (allocator_meta_data *)HEAP_BASE_ADDR;
   size_t curr_size = size << 1;
   int n = *node;

   md->nodes[n].full = false;
   n = NODE_PARENT(n);

   do {

      if (is_block_node_free(md->nodes[n])) {
         break;
      }

      block_node left = md->nodes[NODE_LEFT(n)];
      block_node right = md->nodes[NODE_RIGHT(n)];

      if (!is_block_node_free(left) || !is_block_node_free(right)) {

         DEBUG_printk("STOP: unable to make node %i (size %u) as free\n",
                      n, curr_size);

         DEBUG_printk("node left: free:  %i, split: %i\n",
                      !left.full, left.split);

         DEBUG_printk("node right: free: %i, split: %i\n",
                      !right.full, left.split);

         curr_size >>= 1;
         break;
      }

      *node = n; // last successful coaleshe.

      DEBUG_printk("Marking node = %i (size: %u) as free\n", n, curr_size);

      md->nodes[n].full = false;
      md->nodes[n].split = false;

      n = NODE_PARENT(n);
      curr_size <<= 1;

   } while (n != 0);

   return curr_size;
}

ALWAYS_INLINE static bool node_has_page(int node)
{
   return allocation_for_metadata_nodes[(node * sizeof(block_node)) >> 15];
}

static inline void evenually_allocate_page_for_node(int node)
{
   const uptr index = (node * sizeof(block_node)) >> 15;
   const uptr pages_addr = HEAP_BASE_ADDR + (index << 15);

   if (!allocation_for_metadata_nodes[index]) {

      DEBUG_ONLY(bool success =) kbasic_virtual_alloc(pages_addr, 8);
      ASSERT(success);

      // ASSUMPTION: the value of new_node is just 8 zeros.

      bzero((void *)pages_addr, 8 * PAGE_SIZE);
      allocation_for_metadata_nodes[index] = true;
   }
}

static void *actual_allocate_node(size_t node_size, int node)
{
   allocator_meta_data *md = (allocator_meta_data *)HEAP_BASE_ADDR;
   md->nodes[node].full = true;

   const uptr vaddr = (uptr)node_to_ptr(node, node_size);

   uptr alloc_block_vaddr = vaddr & ~(ALLOC_BLOCK_SIZE - 1);
   const int alloc_block_count = 1 + ((node_size - 1) >> alloc_block_size_log2);

   for (int i = 0; i < alloc_block_count; i++) {

      int alloc_node = ptr_to_node((void *)alloc_block_vaddr, ALLOC_BLOCK_SIZE);
      ASSERT(node_to_ptr(alloc_node,
                         ALLOC_BLOCK_SIZE) == (void *)alloc_block_vaddr);
      evenually_allocate_page_for_node(alloc_node);

      DEBUG_printk("For node# %i, using alloc block (%i/%i): %p (node #%u)\n",
                   node, i+1, alloc_block_count, alloc_block_vaddr, alloc_node);

      if (!md->nodes[alloc_node].allocated) {

         DEBUG_printk("Allocating block of pages..\n");

         DEBUG_ONLY(bool success =)
            kbasic_virtual_alloc(alloc_block_vaddr,
                                 ALLOC_BLOCK_SIZE / PAGE_SIZE);
         ASSERT(success);

         md->nodes[alloc_node].allocated = true;
      }

      if (node_size >= ALLOC_BLOCK_SIZE) {
         ASSERT(!md->nodes[alloc_node].split);
         md->nodes[alloc_node].full = true;
      } else {
         ASSERT(md->nodes[alloc_node].split);
      }

      alloc_block_vaddr += ALLOC_BLOCK_SIZE;
   }

   DEBUG_printk("Returning addr %p (%u alloc blocks)\n",
                vaddr,
                alloc_block_count);

   return (void *)vaddr;
}

ALWAYS_INLINE static void split_node(int node)
{
   allocator_meta_data *md = (allocator_meta_data *)HEAP_BASE_ADDR;

   md->nodes[node].split = true;

   md->nodes[NODE_LEFT(node)].split = false;
   md->nodes[NODE_LEFT(node)].full = false;

   md->nodes[NODE_RIGHT(node)].split = false;
   md->nodes[NODE_RIGHT(node)].full = false;
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
   stack_size--;                               \
   returned = true;                            \
   continue

//////////////////////////////////////////////////////////////////

void *kmalloc(size_t desired_size)
{
   ASSERT(kmalloc_initialized);

   /*
    * ASSERTs that HEAP_BASE_ADDR is aligned at ALLOC_BLOCK_SIZE.
    * Without that condition the "magic" of ptr_to_node() and node_to_ptr()
    * does not work.
    */
   ASSERT((HEAP_BASE_ADDR & (ALLOC_BLOCK_SIZE - 1)) == 0);

   DEBUG_printk("kmalloc(%u)...\n", desired_size);

   if (UNLIKELY(desired_size > HEAP_DATA_SIZE)) {
      return NULL;
   }

   ASSERT(desired_size != 0);

   const size_t size = MAX(desired_size, MIN_BLOCK_SIZE);

   allocator_meta_data * const md = (allocator_meta_data *)HEAP_BASE_ADDR;

   int stack_size = 1;
   bool returned = false;
   stack_elem alloc_stack[32];

   stack_elem base_elem = { HEAP_DATA_SIZE, 0 };
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

      DEBUG_printk("Node# %i, node_size = %u, vaddr = %p\n",
                   node, node_size, vaddr);

      evenually_allocate_page_for_node(node);

      if (node_size > MIN_BLOCK_SIZE) {
         evenually_allocate_page_for_node(left_node);
         evenually_allocate_page_for_node(right_node);
      }

      block_node n = md->nodes[node];

      if (n.full) {
         DEBUG_printk("Not free, return null\n");
         SIMULATE_RETURN_NULL();
      }

      if (half_node_size < size) {

         if (n.split) {
            DEBUG_printk("split, return null\n");
            SIMULATE_RETURN_NULL();
         }

         void *vaddr = actual_allocate_node(node_size, node);

         // Walking up to mark the parent nodes as 'full' if necessary..

         for (int ss = stack_size - 2; ss > 0; ss--) {

            const int n = alloc_stack[ss].node;

            if (md->nodes[NODE_LEFT(n)].full && md->nodes[NODE_RIGHT(n)].full)
               md->nodes[n].full = true;
         }

         return vaddr;
      }


      if (!n.split) {
         DEBUG_printk("Splitting node #%u...\n", node);
         split_node(node);
      }

      if (!md->nodes[left_node].full) {

         DEBUG_printk("going to left..\n");

         SIMULATE_CALL(half_node_size, left_node);

         after_left_call:

         DEBUG_printk("allocation on left node not possible, "
                      "trying with right..\n");

         // The call on the left node returned NULL so, go to the right node.
         SIMULATE_CALL(half_node_size, right_node);

      } else if (!md->nodes[right_node].full) {

         DEBUG_printk("going on right..\n");

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

   if (ptr == NULL) {
      return;
   }

   ASSERT(size != 0);

   size = roundup_next_power_of_2(MAX(size, MIN_BLOCK_SIZE));

   int node = ptr_to_node(ptr, size);

   DEBUG_printk("free_node: node# %i (size %u)\n", node, size);

   ASSERT(node_to_ptr(node, size) == ptr);
   ASSERT(node_has_page(node));

   allocator_meta_data *md = (allocator_meta_data *)HEAP_BASE_ADDR;

   // A node returned to user cannot be split.
   ASSERT(!md->nodes[node].split);


   // Walking up to mark the parent nodes as 'free' if necessary..

   {
      int biggest_free_node = node;
      size_t biggest_free_size = set_free_uplevels(&biggest_free_node, size);

      DEBUG_printk("After coaleshe, biggest_free_node# %i, "
                   "biggest_free_size = %u\n",
                   biggest_free_node, biggest_free_size);

      ASSERT(biggest_free_node == node || biggest_free_size != size);

      if (biggest_free_size < ALLOC_BLOCK_SIZE)
         return;
   }

   uptr alloc_block_vaddr = (uptr)ptr & ~(ALLOC_BLOCK_SIZE - 1);
   const int alloc_block_count = 1 + ((size - 1) >> alloc_block_size_log2);

   DEBUG_printk("The block node used up to %i pages\n", alloc_block_count);

   for (int i = 0; i < alloc_block_count; i++) {

      int alloc_block_node =
         ptr_to_node((void *)alloc_block_vaddr, ALLOC_BLOCK_SIZE);

      ASSERT(node_has_page(alloc_block_node));

      DEBUG_printk("Checking alloc block i = %i, pNode = %i, pAddr = %p, "
                   "alloc = %i, free = %i, split = %i\n",
                   i, alloc_block_node, alloc_block_vaddr,
                   md->nodes[alloc_block_node].allocated,
                   !md->nodes[alloc_block_node].full,
                   md->nodes[alloc_block_node].split);

      /*
       * For nodes smaller than ALLOC_BLOCK_SIZE, the page we're freeing MUST
       * be free. For bigger nodes that kind of checking does not make sense:
       * a major block owns its all pages and their flags are irrelevant.
       */
      ASSERT(size >= ALLOC_BLOCK_SIZE ||
             is_block_node_free(md->nodes[alloc_block_node]));

      ASSERT(md->nodes[alloc_block_node].allocated);

      DEBUG_printk("---> FREEING the ALLOC BLOCK!\n");

      DEBUG_ONLY(bool success =)
         kbasic_virtual_free(alloc_block_vaddr, ALLOC_BLOCK_SIZE / PAGE_SIZE);
      ASSERT(success);

      md->nodes[alloc_block_node] = new_node;
      alloc_block_vaddr += ALLOC_BLOCK_SIZE;
   }
}


void initialize_kmalloc() {

   ASSERT(!kmalloc_initialized);

#ifdef KERNEL_TEST
   bzero(allocation_for_metadata_nodes, sizeof(allocation_for_metadata_nodes));
#endif

   DEBUG_printk("heap base addr: %p\n", HEAP_BASE_ADDR);
   DEBUG_printk("heap data addr: %p\n", HEAP_DATA_ADDR);
   DEBUG_printk("heap size: %u\n", HEAP_DATA_SIZE);


   heap_data_size_log2 = log2_for_power_of_2(HEAP_DATA_SIZE);
   alloc_block_size_log2 = log2_for_power_of_2(ALLOC_BLOCK_SIZE);
   kmalloc_initialized = true;
}

