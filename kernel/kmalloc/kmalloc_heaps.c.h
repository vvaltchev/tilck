/* SPDX-License-Identifier: BSD-2-Clause */

#ifndef _KMALLOC_C_

   #error This is NOT a header file and it is not meant to be included

   /*
    * The only purpose of this file is to keep kmalloc.c shorter.
    * Yes, this file could be turned into a regular C source file, but at the
    * price of making several static functions and variables in kmalloc.c to be
    * just non-static. We don't want that. Code isolation is a GOOD thing.
    */

#endif

#include <tilck/kernel/system_mmap.h>
#include <tilck/kernel/list.h>

STATIC struct kmalloc_heap first_heap_struct;
STATIC struct kmalloc_heap *heaps[KMALLOC_HEAPS_COUNT];
STATIC int used_heaps;
STATIC size_t max_tot_heap_mem_free;

#ifndef UNIT_TEST_ENVIRONMENT

void *kmalloc_get_first_heap(size_t *size)
{
   static char buf[KMALLOC_FIRST_HEAP_SIZE] ALIGNED_AT(KMALLOC_MAX_ALIGN);
   STATIC_ASSERT((KMALLOC_FIRST_HEAP_SIZE % (64 * KB)) == 0);

   if (size)
      *size = KMALLOC_FIRST_HEAP_SIZE;

   return buf;
}

#endif

#include "kmalloc_leak_detector.c.h"

static void
kmalloc_heap_set_pre_calculated_values(struct kmalloc_heap *h)
{
   h->heap_last_byte = h->vaddr + h->size - 1;
   h->heap_data_size_log2 = log2_for_power_of_2(h->size);
   h->alloc_block_size_log2 = log2_for_power_of_2(h->alloc_block_size);
   h->metadata_size = calculate_heap_metadata_size(h->size, h->min_block_size);
}

bool
kmalloc_create_heap(struct kmalloc_heap *h,
                    ulong vaddr,
                    size_t size,
                    size_t min_block_size,
                    size_t alloc_block_size,
                    bool linear_mapping,
                    void *metadata_nodes,
                    virtual_alloc_and_map_func valloc,
                    virtual_free_and_unmap_func vfree)
{
   if (size != SMALL_HEAP_SIZE) {
      // heap size has to be a multiple of KMALLOC_MIN_HEAP_SIZE
      ASSERT((size & (KMALLOC_MIN_HEAP_SIZE - 1)) == 0);

      // heap size must be a power of 2
      ASSERT(roundup_next_power_of_2(size) == size);

      // vaddr must be aligned at least at KMALLOC_MAX_ALIGN
      ASSERT((vaddr & (KMALLOC_MAX_ALIGN - 1)) == 0);
   }

   if (!linear_mapping) {
      // alloc block size has to be a multiple of PAGE_SIZE
      ASSERT((alloc_block_size & (PAGE_SIZE - 1)) == 0);
      ASSERT(alloc_block_size <= KMALLOC_MAX_ALIGN);
   } else {
      ASSERT(alloc_block_size == 0);
   }

   bzero(h, sizeof(*h));
   h->metadata_size = calculate_heap_metadata_size(size, min_block_size);

   h->valloc_and_map = valloc;
   h->vfree_and_unmap = vfree;

   if (!metadata_nodes) {
      // It is OK to pass NULL as 'metadata_nodes' if at least one heap exists.
      ASSERT(heaps[0] != NULL);
      ASSERT(heaps[0]->vaddr != 0);

      metadata_nodes = vmalloc(h->metadata_size);

      if (!metadata_nodes)
         return false;
   }

   h->vaddr = vaddr;
   h->size = size;
   h->min_block_size = min_block_size;
   h->alloc_block_size = alloc_block_size;
   h->metadata_nodes = metadata_nodes;
   h->region = -1;
   kmalloc_heap_set_pre_calculated_values(h);

   bzero(h->metadata_nodes, h->metadata_size);
   h->linear_mapping = linear_mapping;
   return true;
}

struct kmalloc_heap *
kmalloc_create_regular_heap(ulong vaddr,
                            size_t size,
                            size_t min_block_size)
{
   struct kmalloc_heap *h = kalloc_obj(struct kmalloc_heap);
   bool success;

   if (!h)
      return NULL;

   success = kmalloc_create_heap(h,
                                 vaddr,
                                 size,
                                 min_block_size,
                                 0,                /* alloc_block_size */
                                 true,             /* linear_mapping */
                                 NULL,             /* metadata_nodes */
                                 NULL,             /* valloc */
                                 NULL);            /* vfree */

   if (!success) {
      kfree_obj(h, struct kmalloc_heap);
      return NULL;
   }

   return h;
}

void kmalloc_destroy_heap(struct kmalloc_heap *h)
{
   vfree2(h->metadata_nodes, h->metadata_size);
   bzero(h, sizeof(struct kmalloc_heap));
}

struct kmalloc_heap *
kmalloc_heap_dup_expanded(struct kmalloc_heap *h, size_t new_size)
{
   struct kmalloc_heap *new_heap;

   /* `new_size` must at least as big as the old one */
   ASSERT(new_size >= h->size);

   /* `new_size` must be a power of 2 */
   ASSERT(roundup_next_power_of_2(new_size) == new_size);

   if (!h)
      return NULL;

   if (!(new_heap = kalloc_obj(struct kmalloc_heap)))
      return NULL;

   memcpy(new_heap, h, sizeof(struct kmalloc_heap));

   new_heap->size = new_size;
   new_heap->metadata_size =
      calculate_heap_metadata_size(new_size, new_heap->min_block_size);

   new_heap->metadata_nodes = vmalloc(new_heap->metadata_size);

   if (!new_heap->metadata_nodes) {
      kfree_obj(new_heap, struct kmalloc_heap);
      return NULL;
   }

   if (new_size == h->size) {
      memcpy(new_heap->metadata_nodes, h->metadata_nodes, h->metadata_size);
      return new_heap;
   }

   kmalloc_heap_set_pre_calculated_values(new_heap);
   bzero(new_heap->metadata_nodes, new_heap->metadata_size);

   struct block_node *new_nodes = new_heap->metadata_nodes;
   struct block_node *old_nodes = h->metadata_nodes;
   size_t nodes_per_row = 1;
   int new_idx, old_idx;

   for (size_t size = new_heap->size; size >= h->min_block_size; size /= 2) {

      new_idx = ptr_to_node(new_heap, TO_PTR(h->vaddr), size);

      if (size > h->size) {

         new_nodes[new_idx].split = true;

      } else {

         old_idx = ptr_to_node(h, TO_PTR(h->vaddr), size);
         memcpy(&new_nodes[new_idx], &old_nodes[old_idx], nodes_per_row);
         nodes_per_row *= 2;
      }
   }

   return new_heap;
}

struct kmalloc_heap *
kmalloc_heap_dup(struct kmalloc_heap *h)
{
   return kmalloc_heap_dup_expanded(h, h->size);
}

static size_t find_biggest_heap_size(ulong vaddr, ulong limit)
{
   ulong curr_max = 512 * MB;
   ulong curr_end;

   while (curr_max) {

      curr_end = vaddr + curr_max;

      if (vaddr < curr_end && curr_end <= limit)
         break;

      curr_max >>= 1;
   }

   return curr_max;
}

static int kmalloc_internal_add_heap(void *vaddr, size_t heap_size)
{
   const size_t min_block_size = SMALL_HEAP_MAX_ALLOC + 1;
   const size_t metadata_size =
      calculate_heap_metadata_size(heap_size, min_block_size);

   if (used_heaps >= ARRAY_SIZE(heaps))
      return -1;

   if (!used_heaps) {

      heaps[used_heaps] = &first_heap_struct;

   } else {

      heaps[used_heaps] =
         kmalloc(MAX(sizeof(struct kmalloc_heap), SMALL_HEAP_MAX_ALLOC + 1));

      if (!heaps[used_heaps])
         panic("Unable to alloc memory for struct struct kmalloc_heap");
   }

   bool success =
      kmalloc_create_heap(heaps[used_heaps],
                          (ulong)vaddr,
                          heap_size,
                          min_block_size,
                          0,              /* alloc_block_size */
                          true,           /* linear mapping */
                          vaddr,          /* metadata_nodes */
                          NULL, NULL);

   VERIFY(success);
   VERIFY(heaps[used_heaps] != NULL);

   /*
    * We passed to kmalloc_create_heap() the begining of the heap as 'metadata'
    * in order to avoid using another heap (that might not be large enough) for
    * that. Now we MUST register that area in the metadata itself, by doing an
    * allocation using per_heap_kmalloc().
    */

   size_t actual_metadata_size = metadata_size;

   void *md_allocated =
      per_heap_kmalloc(heaps[used_heaps], &actual_metadata_size, 0);

   if (KMALLOC_HEAVY_STATS)
      kmalloc_account_alloc(metadata_size);

   /*
    * We have to be SURE that the allocation returned the very beginning of
    * the heap, as we expected.
    */

   VERIFY(md_allocated == vaddr);
   return used_heaps++;
}

static long greater_than_heap_cmp(const void *a, const void *b)
{
   const struct kmalloc_heap *const *ha_ref = a;
   const struct kmalloc_heap *const *hb_ref = b;

   const struct kmalloc_heap *ha = *ha_ref;
   const struct kmalloc_heap *hb = *hb_ref;

   if (ha->size < hb->size)
      return 1;

   if (ha->size == hb->size)
      return 0;

   return -1;
}

static void
init_kmalloc_fill_region(int region, ulong vaddr, ulong limit, bool dma)
{
   int heap_index;
   vaddr = pow2_round_up_at(
      vaddr,
      MIN(KMALLOC_MIN_HEAP_SIZE, KMALLOC_MAX_ALIGN)
   );

   if (vaddr >= limit)
      return;

   while (true) {

      size_t heap_size = find_biggest_heap_size(vaddr, limit);

      if (heap_size < KMALLOC_MIN_HEAP_SIZE)
         break;

      heap_index = kmalloc_internal_add_heap((void *)vaddr, heap_size);

      if (heap_index < 0) {
         printk("kmalloc: no heap slot for heap at %p, size: %zu KB\n",
                TO_PTR(vaddr), heap_size / KB);
         break;
      }

      heaps[heap_index]->region = region;
      heaps[heap_index]->dma = dma;
      vaddr = heaps[heap_index]->vaddr + heaps[heap_index]->size;
   }
}

void early_init_kmalloc(void)
{
   int heap_index;

   ASSERT(!kmalloc_initialized);
   list_init(&small_heaps_list);
   list_init(&avail_small_heaps_list);

   used_heaps = 0;
   bzero(heaps, sizeof(heaps));

   {
      size_t first_heap_size;
      void *first_heap_ptr;
      first_heap_ptr = kmalloc_get_first_heap(&first_heap_size);
      heap_index = kmalloc_internal_add_heap(first_heap_ptr, first_heap_size);
   }

   VERIFY(heap_index == 0);

   kmalloc_initialized = true; /* we have at least 1 heap */

   if (KMALLOC_HEAVY_STATS) {
      kmalloc_init_heavy_stats();
      kmalloc_account_alloc(heaps[0]->metadata_size);
   }
}

void init_kmalloc(void)
{
   struct mem_region r;
   ulong vbegin, vend;

   ASSERT(kmalloc_initialized);
   ASSERT(used_heaps == 1);

   heaps[0]->region =
      system_mmap_get_region_of(KERNEL_VA_TO_PA(kmalloc_get_first_heap(NULL)));

   for (int i = 0; i < get_mem_regions_count(); i++) {

      get_mem_region(i, &r);

      if (!linear_map_mem_region(&r, &vbegin, &vend))
         break;

      if (r.type == MULTIBOOT_MEMORY_AVAILABLE) {

         const bool dma = r.extra == MEM_REG_EXTRA_DMA;

         if (!r.extra || dma)
            init_kmalloc_fill_region(i, vbegin, vend, dma);

         if (vend == LINEAR_MAPPING_END)
            break;
      }
   }

   insertion_sort_ptr(heaps,
                      (u32)used_heaps,
                      greater_than_heap_cmp);

   for (int i = 0; i < KMALLOC_HEAPS_COUNT; i++) {

      struct kmalloc_heap *h = heaps[i];

      if (!h)
         continue;

      max_tot_heap_mem_free += (h->size - h->mem_allocated);
   }
}

size_t kmalloc_get_max_tot_heap_free(void)
{
   return max_tot_heap_mem_free;
}

void
debug_kmalloc_get_heap_info_by_ptr(struct kmalloc_heap *h,
                                   struct debug_kmalloc_heap_info *i)
{
   *i = (struct debug_kmalloc_heap_info) {
      .vaddr = h->vaddr,
      .size = h->size,
      .mem_allocated = h->mem_allocated,
      .min_block_size = h->min_block_size,
      .alloc_block_size = h->alloc_block_size,
      .region = h->region,
   };
}

bool
debug_kmalloc_get_heap_info(int heap_num, struct debug_kmalloc_heap_info *i)
{
   struct kmalloc_heap *h = heaps[heap_num];

   if (!h)
      return false;

   debug_kmalloc_get_heap_info_by_ptr(h, i);
   return true;
}

void
debug_kmalloc_get_stats(struct debug_kmalloc_stats *stats)
{
   *stats = (struct debug_kmalloc_stats) {
      .small_heaps = shs,
      .chunk_sizes_count =
         KMALLOC_HEAVY_STATS ? alloc_arr_used : 0,
   };
}
