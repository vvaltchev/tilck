
#include <kmalloc.h>
#include <paging.h>
#include <stringUtil.h>

#define MIN_BLOCK_SIZE (32)

#ifndef KERNEL_TEST

#define HEAP_DATA_SIZE (512 * 1024 * 1024)

#else

#define HEAP_DATA_SIZE (1 * 1024 * 1024)

#endif


#define BLOCK_NODES_IN_META_DATA (2 * HEAP_DATA_SIZE / MIN_BLOCK_SIZE)
#define HEAP_DATA_ADDR (HEAP_BASE_ADDR + BLOCK_NODES_IN_META_DATA * sizeof(block_node))


// MIN_BLOCK_SIZE has to be a multiple of 32
STATIC_ASSERT((MIN_BLOCK_SIZE & 31) == 0);

// HEAP_DATA_SIZE has to be a multiple of 1 MB
STATIC_ASSERT((HEAP_DATA_SIZE & ((1 << 20) - 1)) == 0);

bool kbasic_virtual_alloc(uintptr_t vaddr, int pageCount);
bool kbasic_virtual_free(uintptr_t vaddr, int pageCount);



typedef struct {

   uint8_t split : 1; // 1 if the block has been split. Check its children.
   uint8_t free : 1;  // free = 1 means the chunk is free when split = 0,
                      // otherwise, when split = 1, it means there is some free space.

   uint8_t allocated : 1; // used only for nodes of PAGE_SIZE size.

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

CONSTEXPR int ptr_to_node(void *ptr, size_t size)
{
   const int heapSizeLog = log2(HEAP_DATA_SIZE);

   uintptr_t raddr = (uintptr_t)ptr - HEAP_DATA_ADDR;
   int sizeLog = log2(size);
   int nodes_before_our = (1 << (heapSizeLog - sizeLog)) - 1;
   int position_in_row = raddr >> sizeLog;

   return nodes_before_our + position_in_row;
}

CONSTEXPR void *node_to_ptr(int node, size_t size)
{
   const int heapSizeLog = log2(HEAP_DATA_SIZE);

   int sizeLog = log2(size);
   int nodes_before_our = (1 << (heapSizeLog - sizeLog)) - 1;

   int position_in_row = node - nodes_before_our;
   uintptr_t raddr = position_in_row << sizeLog;

   return (void *)(raddr + HEAP_DATA_ADDR);
}

void set_no_free_uplevels(int node)
{
   allocator_meta_data *md = (allocator_meta_data *)HEAP_BASE_ADDR;

   int n = NODE_PARENT(node);

   do {

      if (!md->nodes[NODE_LEFT(n)].free && !md->nodes[NODE_RIGHT(n)].free) {
         md->nodes[n].free = false;
      }

      n = NODE_PARENT(n);

   } while (n != 0);
}

size_t set_free_uplevels(int n, size_t size) {

   allocator_meta_data *md = (allocator_meta_data *)HEAP_BASE_ADDR;
   size_t curr_size = size << 1;

   do {

      if (!md->nodes[NODE_LEFT(n)].free || !md->nodes[NODE_RIGHT(n)].free) {

         //printk("STOP node left free:  %i\n", md->nodes[NODE_LEFT(n)].free);
         //printk("STOP node right free: %i\n", md->nodes[NODE_RIGHT(n)].free);
         break; // break on the first node that cannot be coaleshed.
      }

      //printk("Marking parent node = %i (size: %u) as free\n", n, curr_size);

      md->nodes[n].free = true;
      md->nodes[n].split = false;

      n = NODE_PARENT(n);
      curr_size <<= 1;

   } while (n != 0);

   return curr_size;
}

void evenually_allocate_page_for_node(int node)
{
   allocator_meta_data *md = (allocator_meta_data *)HEAP_BASE_ADDR;
   uintptr_t pageAddr = (uintptr_t)(md->nodes + node) & ~(PAGE_SIZE - 1);

   ////printk("evenually_allocate_page_for_node(%i): page: %p\n", node, pageAddr);

   if (!is_mapped(get_kernel_page_dir(), pageAddr)) {

      //printk("Allocating page %p for node# %i\n", pageAddr, node);

      bool success = kbasic_virtual_alloc(pageAddr, 1);
      ASSERT(success);

      block_node new_node;
      new_node.free = true;
      new_node.allocated = false;
      new_node.split = false;

      for (unsigned i = 0; i < PAGE_SIZE/sizeof(block_node); i++) {
         ((block_node *)pageAddr)[i] = new_node;
      }
   }
}


bool node_has_page(int node)
{
   allocator_meta_data *md = (allocator_meta_data *)HEAP_BASE_ADDR;
   uintptr_t pageAddr = (uintptr_t)(md->nodes + node) & ~(PAGE_SIZE - 1);

   return is_mapped(get_kernel_page_dir(), pageAddr);
}


void *allocate_node_rec(size_t size, size_t node_size, int node, uintptr_t vaddr)
{
   allocator_meta_data *md = (allocator_meta_data *)HEAP_BASE_ADDR;

   //printk("allocate_node_rec(node_size = %u, node index = %i, vaddr = %p\n", node_size, node, vaddr);

   //debug_print_node_state(256);

   evenually_allocate_page_for_node(node);

   if (node_size > MIN_BLOCK_SIZE) {
      evenually_allocate_page_for_node(NODE_LEFT(node));
      evenually_allocate_page_for_node(NODE_RIGHT(node));
   }

   block_node n = md->nodes[node];

   if (!n.free) {
      //printk("Node is not free, return NULL\n");
      return NULL;
   }

   if (HALF(node_size) < size) {

      ////printk("The node size is correct\n");

      if (n.split) {

         //printk("The node is split, returning NULL\n");
         return NULL;
      }

      md->nodes[node].free = false;

      // Walking up to mark the parent node as 'not free' if necessary..
      set_no_free_uplevels(node);

      ASSERT((void *) vaddr == node_to_ptr(node, node_size));

      uintptr_t pageOfVaddr = vaddr & ~(PAGE_SIZE - 1);
      int pageCount = 1 + ( (node_size - 1) >> log2(PAGE_SIZE) );

      //printk("For that node, I'd need %i pages\n", pageCount);

      for (int i = 0; i < pageCount; i++) {
         
         int pageNode = ptr_to_node((void *) pageOfVaddr, PAGE_SIZE);

         //printk("i = %i, pageNode = %i, pageAddr = %p\n", i, pageNode, pageOfVaddr);
         evenually_allocate_page_for_node(pageNode);


         if (!md->nodes[pageNode].allocated) {
            bool success = kbasic_virtual_alloc(pageOfVaddr, 1);
            ASSERT(success);

            md->nodes[pageNode].allocated = true;

         } else {

            //printk("Page %p is already allocated, skipping..\n", pageOfVaddr);
         }

         if (node_size >= PAGE_SIZE) {
            md->nodes[pageNode].free = false;
            //set_no_free_uplevels(pageNode);
         }

         pageOfVaddr += PAGE_SIZE;
      }

      //printk("allocate_node_rec: returning vaddr = %p for node# %i (parent: %i)\n", vaddr, node, NODE_PARENT(node));
      return (void *) vaddr;
   }
   
   ////printk("The node size is bigger than necessary.. going to children\n");

   // node_size / 2 >= size

   if (!n.split) {

      ////printk("Splitting the node..\n");

      // The node is free and not split: split it and allocate in the children.
      md->nodes[node].split = true;

      md->nodes[NODE_LEFT(node)].split = false;
      md->nodes[NODE_LEFT(node)].free = true;

      md->nodes[NODE_RIGHT(node)].split = false;
      md->nodes[NODE_RIGHT(node)].free = true;
   }

   if (md->nodes[NODE_LEFT(node)].free) {

      //printk("Left node has free space\n");
      void *res = allocate_node_rec(size, HALF(node_size), NODE_LEFT(node), vaddr);

      if (!res) {
         //printk("Left node returned NULL, going to right node\n");
         res = allocate_node_rec(size, HALF(node_size), NODE_RIGHT(node), vaddr + HALF(node_size));
      }

      return res;

   } else if (md->nodes[NODE_RIGHT(node)].free) {

      //printk("The right node has free space\n");
      return allocate_node_rec(size, HALF(node_size), NODE_RIGHT(node), vaddr + HALF(node_size));
   }

   //printk("Nothing was found neither in the left nor the right node\n");
   return NULL;
}


void *allocate_node(size_t size)
{
   int node = 0;
   size_t node_size = HEAP_DATA_SIZE;

   if (UNLIKELY(size > HEAP_DATA_SIZE)) {
      return NULL;
   }

   size = MAX(size, MIN_BLOCK_SIZE);
 
   return allocate_node_rec(size, node_size, node, HEAP_DATA_ADDR);
}

//void debug_print_node_state(int node)
//{
//   evenually_allocate_page_for_node(node);
//
//   allocator_meta_data *md = (allocator_meta_data *)HEAP_BASE_ADDR;
//
//   block_node n = md->nodes[node];
//
//   //printk("[Node #%i] free: %i, split: %i, allocated: %i\n", node, n.free, n.split, n.allocated);
//}


void free_node(void *ptr, size_t size)
{
   int node = ptr_to_node(ptr, size);

   //printk("free_node: node# %i\n", node);

   ASSERT(node_to_ptr(node, size) == ptr);
   ASSERT(node_has_page(node));

   allocator_meta_data *md = (allocator_meta_data *)HEAP_BASE_ADDR;

   md->nodes[node].free = true;

   // Walking up to mark the parent nodes as 'free' if necessary..

   int n = NODE_PARENT(node);
   
   size_t curr_size = set_free_uplevels(n, size);

   //printk("After coaleshe, n = %i, curr_size = %u\n", n, curr_size);
   
   if (curr_size < PAGE_SIZE)
      return;

   uintptr_t pageOfVaddr = (uintptr_t)ptr & ~(PAGE_SIZE - 1);
   int pageCount = 1 + ((size - 1) >> log2(PAGE_SIZE));

   //printk("The block node used up to %i pages\n", pageCount);

   for (int i = 0; i < pageCount; i++) {

      int pageNode = ptr_to_node((void *)pageOfVaddr, PAGE_SIZE);
      ASSERT(node_has_page(pageNode));

      //printk("i = %i, pNode = %i, pAddr = %p, alloc = %i, free = %i, split = %i\n",
      //       i, pageNode, pageOfVaddr, md->nodes[pageNode].allocated, md->nodes[pageNode].free, md->nodes[pageNode].split);

      ASSERT(md->nodes[pageNode].allocated);
      bool success = kbasic_virtual_free(pageOfVaddr, 1);
      ASSERT(success);

      block_node new_node_val;
      new_node_val.allocated = false;
      new_node_val.free = true;
      new_node_val.split = false;

      md->nodes[pageNode] = new_node_val;

      pageOfVaddr += PAGE_SIZE;
   }
}


void initialize_kmalloc() {

   /* Do nothing, for the moment. */

   //printk("heap base addr: %p\n", HEAP_BASE_ADDR);
   //printk("heap data addr: %p\n", HEAP_DATA_ADDR);
}

void *kmalloc(size_t size)
{
	//printk("kmalloc(%i)\n", size);
	return allocate_node(size);
}


void kfree(void *ptr, size_t size)
{
	//printk("free(%p, %u)\n", ptr, size);

   size = MAX(size, MIN_BLOCK_SIZE);
   free_node(ptr, roundup_next_power_of_2(size));
}