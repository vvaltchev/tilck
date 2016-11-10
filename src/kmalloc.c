
#include <kmalloc.h>
#include <paging.h>
#include <stringUtil.h>

#define MIN_BLOCK_SIZE (32)
#define BIG_CHUNK_SIZE (1 << 20) // 1 MB
#define BIG_CHUNK_COUNT (128) // Total of 128 MB


#define BLOCK_NODES_IN_META_DATA_OBJ (2 * BIG_CHUNK_SIZE / MIN_BLOCK_SIZE)
#define CHUNKS_IN_METADATA_CHUNK (BIG_CHUNK_SIZE / BLOCK_NODES_IN_META_DATA_OBJ)

// Reasonble sizes for MIN_BLOCK_SIZE
static_assert(MIN_BLOCK_SIZE == 16 || MIN_BLOCK_SIZE == 32 || MIN_BLOCK_SIZE == 64);

// Reasonable size for BIG_CHUNK_SIZE (divisible by 1 MB)
static_assert((BIG_CHUNK_SIZE & ((1 << 20) - 1)) == 0);

bool kbasic_virtual_alloc(uintptr_t vaddr, size_t size);
bool kbasic_virtual_free(uintptr_t vaddr, size_t size);

/*
 * Table for accounting slots of 1 MB used in kernel's virtual memory.
 *
 * Each chunk is 'false' if there is some space left or 'true' if its whole
 * memory has been used.
 *
 */

bool full_chunks_table[BIG_CHUNK_COUNT] = {0};


/*
 * Each bool here is true if the slot has been initialized.
 * Initialized means that the memory for it has been claimed
 */

bool initialized_slots[BIG_CHUNK_COUNT] = {0};

typedef struct {

   uint8_t split : 1; // 1 if the block has been split. Check its children.
   uint8_t free : 1;  // In case split = 0, free = 1 means the chunk is free.

   uint8_t unused : 6;

} block_node;


typedef struct {

   block_node nodes[BLOCK_NODES_IN_META_DATA_OBJ];

} chunk_meta_data_obj;


typedef struct {

   chunk_meta_data_obj meta_data_objects[CHUNKS_IN_METADATA_CHUNK];

} meta_data_chunk;

static_assert(sizeof(meta_data_chunk) == BIG_CHUNK_SIZE);

int get_free_chunk_index()
{
   /*
   * Only chunks at index N, with N not divisible by
   * CHUNKS_IN_METADATA_CHUNK are usable for data.
   * Chunks with index divisible by CHUNKS_IN_METADATA_CHUNK, are used for
   * allocator's meta-data.
   *
   * In order to keep things simple and handy, a meta-data chunk contains
   * meta-data for the next (CHUNKS_IN_METADATA_CHUNK - 1) chunks.
   * That way, given a chunk at index N, we know that its meta-data chunk is
   * floor(N / CHUNKS_IN_METADATA_CHUNK) and its meta-data is the chunk_meta_data_obj
   * at index N & (CHUNKS_IN_METADATA_CHUNK - 1)
   */

   for (int i = 0; i < BIG_CHUNK_COUNT; i++)
      if ((i & (CHUNKS_IN_METADATA_CHUNK - 1)) && !full_chunks_table[i])
         return i;

   return -1;
}



void *kmalloc(size_t size)
{
	printk("kmalloc(%i)\n", size);
   printk("heap base addr: %p\n", HEAP_BASE_ADDR);

   bool r = kbasic_virtual_alloc(HEAP_BASE_ADDR, 4096);

   ASSERT(r);


	return (void*)HEAP_BASE_ADDR;
}


void kfree(void *ptr, size_t size)
{
	printk("free(%p, %u)\n", ptr, size);
}