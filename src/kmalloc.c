
#include <kmalloc.h>
#include <paging.h>
#include <stringUtil.h>

#define MIN_BLOCK_SIZE (32)

#define BLOCK_NODES_IN_META_DATA (2 * HEAP_DATA_SIZE / MIN_BLOCK_SIZE)
#define HEAP_DATA_ADDR (HEAP_BASE_ADDR + BLOCK_NODES_IN_META_DATA * sizeof(block_node))


// MIN_BLOCK_SIZE has to be a multiple of 32
STATIC_ASSERT((MIN_BLOCK_SIZE & 31) == 0);

// HEAP_DATA_SIZE has to be a multiple of 1 MB
STATIC_ASSERT((HEAP_DATA_SIZE & ((1 << 20) - 1)) == 0);

bool kbasic_virtual_alloc(uintptr_t vaddr, int pageCount);
bool kbasic_virtual_free(uintptr_t vaddr, int pageCount);



typedef struct {

   // 1 if the block has been split. Check its children.
   uint8_t split : 1;

   // 1 means the chunk is completely free when split = 0,
   // otherwise (when split = 1), it means there is some free space.
   uint8_t has_some_free_space : 1;

   uint8_t allocated : 1; // used only for nodes having size=PAGE_SIZE.

   uint8_t unused : 5;

} block_node;

STATIC_ASSERT(sizeof(block_node) == 1);

typedef struct {

   block_node nodes[BLOCK_NODES_IN_META_DATA];

} allocator_meta_data;


#define HALF(x) ((x) >> 1)
#define TWICE(x) ((x) << 1)

#define NODE_LEFT(n) (TWICE(n) + 1)
#define NODE_RIGHT(n) (TWICE(n) + 2)
#define NODE_PARENT(n) (HALF(n-1))
#define NODE_IS_LEFT(n) (((n) & 1) != 0)

CONSTEXPR int ptr_to_node(void *ptr, size_t size)
{
   const int heapSizeLog = log2_for_power_of_2(HEAP_DATA_SIZE);

   uintptr_t raddr = (uintptr_t)ptr - HEAP_DATA_ADDR;
   int sizeLog = log2_for_power_of_2(size);
   int nodes_before_our = (1 << (heapSizeLog - sizeLog)) - 1;
   int position_in_row = raddr >> sizeLog;

   return nodes_before_our + position_in_row;
}

CONSTEXPR void *node_to_ptr(int node, size_t size)
{
   const int heapSizeLog = log2_for_power_of_2(HEAP_DATA_SIZE);

   int sizeLog = log2_for_power_of_2(size);
   int nodes_before_our = (1 << (heapSizeLog - sizeLog)) - 1;

   int position_in_row = node - nodes_before_our;
   uintptr_t raddr = position_in_row << sizeLog;

   return (void *)(raddr + HEAP_DATA_ADDR);
}

static void set_no_free_uplevels(int node)
{
   allocator_meta_data *md = (allocator_meta_data *)HEAP_BASE_ADDR;

   int n = NODE_PARENT(node);

   do {

      if (!md->nodes[NODE_LEFT(n)].has_some_free_space &&
          !md->nodes[NODE_RIGHT(n)].has_some_free_space) {
         md->nodes[n].has_some_free_space = false;
      }

      n = NODE_PARENT(n);

   } while (n > 0);
}

CONSTEXPR static ALWAYS_INLINE bool is_block_node_free(block_node n)
{
   return n.has_some_free_space && !n.split;
}

static size_t set_free_uplevels(int n, size_t size) {

   allocator_meta_data *md = (allocator_meta_data *)HEAP_BASE_ADDR;
   size_t curr_size = size << 1;

   do {

      block_node left = md->nodes[NODE_LEFT(n)];
      block_node right = md->nodes[NODE_RIGHT(n)];

      if (!is_block_node_free(left) || !is_block_node_free(right)) {

         printk("STOP: unable to make node %i (size %u) as free\n", n, curr_size);
         printk("node left free:  %i\n", md->nodes[NODE_LEFT(n)].has_some_free_space);
         printk("node right free: %i\n", md->nodes[NODE_RIGHT(n)].has_some_free_space);

         curr_size >>= 1;
         break;
      }

      printk("Marking node = %i (size: %u) as free\n", n, curr_size);

      md->nodes[n].has_some_free_space = true;
      md->nodes[n].split = false;

      n = NODE_PARENT(n);
      curr_size <<= 1;

   } while (n != 0);

   return curr_size;
}

/*
 * Each byte represents 8 * PAGE_SIZE bytes = 32 KB.
 */

#define ALLOC_METADATA_SIZE sizeof(block_node) * BLOCK_NODES_IN_META_DATA / (8 * PAGE_SIZE)
bool allocation_for_metadata_nodes[ALLOC_METADATA_SIZE] = {0};

ALWAYS_INLINE static bool node_has_page(int node)
{  
   return allocation_for_metadata_nodes[(node * sizeof(block_node)) >> 15];
}


void evenually_allocate_page_for_node(int node)
{
   block_node new_node;
   new_node.split = false;
   new_node.has_some_free_space = true;
   new_node.allocated = false;

   uintptr_t index = (node * sizeof(block_node)) >> 15;
   uintptr_t pagesAddr = HEAP_BASE_ADDR + (index << 15);

   //allocator_meta_data *md = (allocator_meta_data *)HEAP_BASE_ADDR; 
   //uintptr_t pageAddr = (uintptr_t)(md->nodes + node) & ~(PAGE_SIZE - 1);

   //printk("evenually_allocate_page_for_node(%i): index = %u, pagesAddr: %p\n", node, index, pagesAddr);

   if (!allocation_for_metadata_nodes[index]) {

      //printk("Allocating 8 pages at %p for node# %i\n", pagesAddr, node);

      bool success = kbasic_virtual_alloc(pagesAddr, 8);
      ASSERT(success);

      for (unsigned i = 0; i < 8 * PAGE_SIZE/sizeof(block_node); i++) {
         ((block_node *)pagesAddr)[i] = new_node;
      }

      allocation_for_metadata_nodes[index] = true;
   }
}


static void actual_allocate_node(size_t node_size, int node, uintptr_t vaddr)
{
   allocator_meta_data *md = (allocator_meta_data *)HEAP_BASE_ADDR;

   md->nodes[node].has_some_free_space = false;

   // Walking up to mark the parent node as 'not free' if necessary..
   set_no_free_uplevels(node);

   ASSERT((void *)vaddr == node_to_ptr(node, node_size));

   uintptr_t pageOfVaddr = vaddr & ~(PAGE_SIZE - 1);
   int pageCount = 1 + ((node_size - 1) >> log2_for_power_of_2(PAGE_SIZE));

   for (int i = 0; i < pageCount; i++) {

      int pageNode = ptr_to_node((void *)pageOfVaddr, PAGE_SIZE);
      evenually_allocate_page_for_node(pageNode);

      if (!md->nodes[pageNode].allocated) {
         bool success = kbasic_virtual_alloc(pageOfVaddr, 1);
         ASSERT(success);

         md->nodes[pageNode].allocated = true;
      }

      if (node_size >= PAGE_SIZE) {
         md->nodes[pageNode].has_some_free_space = false;
      }

      pageOfVaddr += PAGE_SIZE;
   }

   printk("Returning addr %p (%u pages)\n", vaddr, pageCount);
}

ALWAYS_INLINE static void split_node(int node)
{
   allocator_meta_data *md = (allocator_meta_data *)HEAP_BASE_ADDR;

   md->nodes[node].split = true;

   md->nodes[NODE_LEFT(node)].split = false;
   md->nodes[NODE_LEFT(node)].has_some_free_space = true;

   md->nodes[NODE_RIGHT(node)].split = false;
   md->nodes[NODE_RIGHT(node)].has_some_free_space = true;
}

//////////////////////////////////////////////////////////////////

typedef struct {

   size_t node_size;
   uintptr_t vaddr;
   int node;

} stack_elem;


#define SIMULATE_CALL(a1, a2, a3)              \
   {                                           \
      stack_elem _elem_ = {(a1), (a2), (a3)};  \
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
   if (UNLIKELY(desired_size > HEAP_DATA_SIZE)) {
      return NULL;
   }

   const size_t size = MAX(desired_size, MIN_BLOCK_SIZE);

   allocator_meta_data * const md = (allocator_meta_data *)HEAP_BASE_ADDR;

   int stack_size = 1;
   bool returned = false;
   stack_elem alloc_stack[32];

   stack_elem base_elem = { HEAP_DATA_SIZE, HEAP_DATA_ADDR, 0 };
   alloc_stack[0] = base_elem;

   while (stack_size) {

      // Load the "stack" (function arguments)
      const size_t node_size = alloc_stack[stack_size - 1].node_size;
      const uintptr_t vaddr = alloc_stack[stack_size - 1].vaddr;
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

      printk("Node# %i, node_size = %u\n", node, node_size);

      evenually_allocate_page_for_node(node);

      if (node_size > MIN_BLOCK_SIZE) {
         evenually_allocate_page_for_node(left_node);
         evenually_allocate_page_for_node(right_node);
      }

      block_node n = md->nodes[node];

      if (!n.has_some_free_space) {
         printk("Not free, return null\n");
         SIMULATE_RETURN_NULL();
      }

      if (half_node_size < size) {

         if (n.split) {
            printk("split, return null\n");
            SIMULATE_RETURN_NULL();
         }

         actual_allocate_node(node_size, node, vaddr);
         return (void *) vaddr;
      }
   

      if (!n.split) {
         split_node(node);
      }

      if (md->nodes[left_node].has_some_free_space) {

         printk("going to left..\n");

         SIMULATE_CALL(half_node_size, vaddr, left_node);

         after_left_call:

         printk("allocation on left node not possible, trying with right..\n");

         // The call on the left node returned NULL so, go to the right node.
         SIMULATE_CALL(half_node_size, vaddr + half_node_size, right_node);

      } else if (md->nodes[right_node].has_some_free_space) {

         printk("going on right..\n");

         SIMULATE_CALL(half_node_size, vaddr + half_node_size, right_node);

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
   size = roundup_next_power_of_2(MAX(size, MIN_BLOCK_SIZE));

   int node = ptr_to_node(ptr, size);

   printk("free_node: node# %i\n", node);

   ASSERT(node_to_ptr(node, size) == ptr);
   ASSERT(node_has_page(node));

   allocator_meta_data *md = (allocator_meta_data *)HEAP_BASE_ADDR;

   // A node returned to user cannot be split.
   ASSERT(!md->nodes[node].split);

   md->nodes[node].has_some_free_space = true;

   // Walking up to mark the parent nodes as 'free' if necessary..  
   size_t curr_size = set_free_uplevels(NODE_PARENT(node), size);

   printk("After coaleshe, curr_size = %u\n", curr_size);
   
   if (curr_size < PAGE_SIZE)
      return;

   uintptr_t pageOfVaddr = (uintptr_t)ptr & ~(PAGE_SIZE - 1);
   int pageCount = 1 + ((size - 1) >> log2_for_power_of_2(PAGE_SIZE));

   printk("The block node used up to %i pages\n", pageCount);

   for (int i = 0; i < pageCount; i++) {

      int pageNode = ptr_to_node((void *)pageOfVaddr, PAGE_SIZE);
      ASSERT(node_has_page(pageNode));

      printk("Checking page i = %i, pNode = %i, pAddr = %p, alloc = %i, free = %i, split = %i\n",
         i, pageNode, pageOfVaddr, md->nodes[pageNode].allocated,
         md->nodes[pageNode].has_some_free_space, md->nodes[pageNode].split);

      /*
       * For nodes smaller than PAGE_SIZE, the page we're freeing MUST be free.
       * For bigger nodes that kind of checking does not make sense:
       * a major block owns its all pages and their flags are irrelevant.
       */
      ASSERT(size > PAGE_SIZE || md->nodes[pageNode].has_some_free_space);

      // ASSERT that the page is currently marked as allocated.
      ASSERT(md->nodes[pageNode].allocated);

      printk("---> FREEING the PAGE!\n");

      bool success = kbasic_virtual_free(pageOfVaddr, 1);
      ASSERT(success);

      block_node new_node_val;
      new_node_val.allocated = false;
      new_node_val.has_some_free_space = true;
      new_node_val.split = false;

      md->nodes[pageNode] = new_node_val;

      pageOfVaddr += PAGE_SIZE;
   }
}


void initialize_kmalloc() {

   /* Do nothing, for the moment. */

   printk("heap base addr: %p\n", HEAP_BASE_ADDR);
   printk("heap data addr: %p\n", HEAP_DATA_ADDR);
}

