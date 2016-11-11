
#include <kmalloc.h>
#include <paging.h>
#include <stringUtil.h>

#define MIN_BLOCK_SIZE (32)
#define HEAP_DATA_SIZE (512 * 1024 * 1024)

#define BLOCK_NODES_IN_META_DATA (2 * HEAP_DATA_SIZE / MIN_BLOCK_SIZE)
#define HEAP_DATA_ADDR (HEAP_BASE_ADDR + BLOCK_NODES_IN_META_DATA * sizeof(block_node))


// MIN_BLOCK_SIZE has to be a multiple of 32
static_assert((MIN_BLOCK_SIZE & 31) == 0);

// HEAP_DATA_SIZE has to be a multiple of 1 MB
static_assert((HEAP_DATA_SIZE & ((1 << 20) - 1)) == 0);

bool kbasic_virtual_alloc(uintptr_t vaddr, size_t size);
bool kbasic_virtual_free(uintptr_t vaddr, size_t size);



typedef struct {

   uint8_t split : 1; // 1 if the block has been split. Check its children.
   uint8_t free : 1;  // free = 1 means the chunk is free when split = 0,
                      // otherwise, when split = 1, it means there is some free space.

   uint8_t data_allocated : 1;

   uint8_t unused : 5;

} block_node;

static_assert(sizeof(block_node) == 1);

typedef struct {

   block_node nodes[BLOCK_NODES_IN_META_DATA];

} allocator_meta_data;


#define HALF(x) ((x) >> 1)
#define TWICE(x) ((x) << 1)

#define NODE_LEFT(n) (TWICE(n) + 1)
#define NODE_RIGHT(n) (TWICE(n) + 2)
#define NODE_PARENT(n) (HALF(n-1))

void *allocate_node_rec(size_t size, size_t node_size, int node, uintptr_t vaddr)
{
   allocator_meta_data *md = (allocator_meta_data *)HEAP_BASE_ADDR;

   if ((node & (PAGE_SIZE - 1)) == 0) {

      // &md->nodes[node] is on page boundary

      if (!is_mapped(get_kernel_page_dir(), (uintptr_t) &md->nodes[node])) {

         printk("Allocating one page for node %i at addr: %p\n", node, &md->nodes[node]);

         bool success = kbasic_virtual_alloc((uintptr_t) &md->nodes[node], PAGE_SIZE);
         ASSERT(success);
      }
   }

   block_node n = md->nodes[node];

   if (!n.free) {
      return NULL;
   }

   if (HALF(node_size) < size) {

      if (n.split) {
         return NULL;
      }

      return (void *) vaddr;
   }
   
   // node_size / 2 >= size

   if (!n.split) {

      // The node is free and not split: split it and allocate in the children.

      md->nodes[node].split = true;
   }

   if (md->nodes[NODE_LEFT(node)].free) {

      void *res = allocate_node_rec(size, HALF(node_size), NODE_LEFT(node), vaddr);

      if (!res) {
         res = allocate_node_rec(size, HALF(node_size), NODE_RIGHT(node), vaddr + HALF(node_size));
      }

      return res;

   } else if (md->nodes[NODE_RIGHT(node)].free) {

      return allocate_node_rec(size, HALF(node_size), NODE_LEFT(node), vaddr + HALF(node_size));
   }

   return NULL;
}

void *allocate_node(size_t size)
{
   allocator_meta_data *md = (allocator_meta_data *)HEAP_BASE_ADDR;

   int node = 0;
   size_t node_size = HEAP_DATA_SIZE;

   if (UNLIKELY(!md->nodes[node].free || size > HEAP_DATA_SIZE)) {
      return NULL;
   }

   size = MAX(size, MIN_BLOCK_SIZE);
 
   return allocate_node_rec(size, node_size, node, HEAP_DATA_ADDR);
}


void initialize_kmalloc() {

   bool success;
   allocator_meta_data *md = (allocator_meta_data *)HEAP_BASE_ADDR;

   success = kbasic_virtual_alloc(HEAP_BASE_ADDR, PAGE_SIZE);
   ASSERT(success);

   md->nodes[0].split = 0;
   md->nodes[0].free = 1;
   md->nodes[0].data_allocated = 0;
}

void *kmalloc(size_t size)
{
	printk("kmalloc(%i)\n", size);
   //printk("heap base addr: %p\n", HEAP_BASE_ADDR);

   


	return 0;
}


void kfree(void *ptr, size_t size)
{
	printk("free(%p, %u)\n", ptr, size);
}