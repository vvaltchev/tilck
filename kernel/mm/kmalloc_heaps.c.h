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

STATIC kmalloc_heap first_heap_struct;
STATIC kmalloc_heap *heaps[KMALLOC_HEAPS_COUNT];
STATIC u32 used_heaps;
STATIC size_t max_tot_heap_mem_free;

#ifndef UNIT_TEST_ENVIRONMENT

void *kmalloc_get_first_heap(size_t *size)
{
   static char buf[KMALLOC_FIRST_HEAP_SIZE] ALIGNED_AT(KMALLOC_MAX_ALIGN);

   if (size)
      *size = KMALLOC_FIRST_HEAP_SIZE;

   return buf;
}

#endif

#include "kmalloc_leak_detector.c.h"

size_t kmalloc_get_total_heap_allocation(void)
{
   size_t tot = 0;
   disable_preemption();

   for (u32 i = 0; i < used_heaps; i++) {
      tot += heaps[i]->mem_allocated;
   }

   enable_preemption();
   return tot;
}

bool kmalloc_create_heap(kmalloc_heap *h,
                         uptr vaddr,
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

      metadata_nodes = kmalloc(h->metadata_size);

      if (!metadata_nodes)
         return false;
   }

   h->vaddr = vaddr;
   h->size = size;
   h->min_block_size = min_block_size;
   h->alloc_block_size = alloc_block_size;
   h->metadata_nodes = metadata_nodes;
   h->region = -1;

   h->heap_last_byte = vaddr + size - 1;
   h->heap_data_size_log2 = log2_for_power_of_2(size);
   h->alloc_block_size_log2 = log2_for_power_of_2(alloc_block_size);

   bzero(h->metadata_nodes, calculate_heap_metadata_size(size, min_block_size));
   h->linear_mapping = linear_mapping;
   return true;
}

void kmalloc_destroy_heap(kmalloc_heap *h)
{
   kfree2(h->metadata_nodes, h->metadata_size);
   bzero(h, sizeof(kmalloc_heap));
}

kmalloc_heap *kmalloc_heap_dup(kmalloc_heap *h)
{
   if (!h)
      return NULL;

   kmalloc_heap *new_heap = kmalloc(sizeof(kmalloc_heap));

   if (!new_heap)
      return NULL;

   memcpy(new_heap, h, sizeof(kmalloc_heap));

   new_heap->metadata_nodes = kmalloc(h->metadata_size);

   if (!new_heap->metadata_nodes) {
      kfree2(new_heap, sizeof(kmalloc_heap));
      return NULL;
   }

   memcpy(new_heap->metadata_nodes, h->metadata_nodes, h->metadata_size);
   return new_heap;
}

static size_t find_biggest_heap_size(uptr vaddr, uptr limit)
{
   uptr curr_max = 512 * MB;
   uptr curr_end;

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
         kmalloc(MAX(sizeof(kmalloc_heap), SMALL_HEAP_MAX_ALLOC + 1));

      if (!heaps[used_heaps])
         panic("Unable to alloc memory for struct kmalloc_heap");
   }

   bool success =
      kmalloc_create_heap(heaps[used_heaps],
                          (uptr)vaddr,
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
   return (int)used_heaps++;
}

static sptr greater_than_heap_cmp(const void *a, const void *b)
{
   const kmalloc_heap *const *ha_ref = a;
   const kmalloc_heap *const *hb_ref = b;

   const kmalloc_heap *ha = *ha_ref;
   const kmalloc_heap *hb = *hb_ref;

   if (ha->size < hb->size)
      return 1;

   if (ha->size == hb->size)
      return 0;

   return -1;
}

static void init_kmalloc_fill_region(int region, uptr vaddr, uptr limit)
{
   int heap_index;
   vaddr = round_up_at(vaddr, MIN(KMALLOC_MIN_HEAP_SIZE, KMALLOC_MAX_ALIGN));

   if (vaddr >= limit)
      return;

   while (true) {

      size_t heap_size = find_biggest_heap_size(vaddr, limit);

      if (heap_size < KMALLOC_MIN_HEAP_SIZE)
         break;

      heap_index = kmalloc_internal_add_heap((void *)vaddr, heap_size);

      if (heap_index < 0) {
         printk("kmalloc: no heap slot for heap at %p, size: %u KB\n",
                vaddr, heap_size / KB);
         break;
      }

      heaps[heap_index]->region = region;
      vaddr = heaps[heap_index]->vaddr + heaps[heap_index]->size;
   }
}

void init_kmalloc(void)
{
   ASSERT(!kmalloc_initialized);
   list_init(&small_not_full_heaps_list);

   int heap_index;

   used_heaps = 0;
   bzero(heaps, sizeof(heaps));

   {
      size_t first_heap_size;
      void *first_heap_ptr;
      first_heap_ptr = kmalloc_get_first_heap(&first_heap_size);
      heap_index = kmalloc_internal_add_heap(first_heap_ptr, first_heap_size);
   }

   VERIFY(heap_index == 0);

   heaps[heap_index]->region =
      system_mmap_get_region_of(KERNEL_VA_TO_PA(kmalloc_get_first_heap(NULL)));

   kmalloc_initialized = true; /* we have at least 1 heap */

   if (KMALLOC_HEAVY_STATS) {
      kmalloc_init_heavy_stats();
      kmalloc_account_alloc(heaps[0]->metadata_size);
   }

   for (int i = 0; i < mem_regions_count; i++) {

      memory_region_t *r = mem_regions + i;
      uptr vbegin, vend;

      if (!linear_map_mem_region(r, &vbegin, &vend))
         break;

      if (r->type == MULTIBOOT_MEMORY_AVAILABLE) {

         init_kmalloc_fill_region(i, vbegin, vend);

         if (vend == LINEAR_MAPPING_END)
            break;
      }
   }

   insertion_sort_ptr(heaps,
                      used_heaps,
                      greater_than_heap_cmp);

   for (int i = 0; i < KMALLOC_HEAPS_COUNT; i++) {

      kmalloc_heap *h = heaps[i];

      if (!h)
         continue;

      max_tot_heap_mem_free += (h->size - h->mem_allocated);
   }
}

size_t kmalloc_get_max_tot_heap_free(void)
{
   return max_tot_heap_mem_free;
}

bool
debug_kmalloc_get_heap_info(int heap_num, debug_kmalloc_heap_info *i)
{
   kmalloc_heap *h = heaps[heap_num];

   if (!h)
      return false;

   *i = (debug_kmalloc_heap_info) {
      .vaddr = h->vaddr,
      .size = h->size,
      .mem_allocated = h->mem_allocated,
      .min_block_size = h->min_block_size,
      .alloc_block_size = h->alloc_block_size,
      .region = h->region,
   };

   return true;
}

void
debug_kmalloc_get_stats(debug_kmalloc_stats *stats)
{
   *stats = (debug_kmalloc_stats) {
      .small_heaps = shs,
      .chunk_sizes_count =
         KMALLOC_HEAVY_STATS ? alloc_arr_used : 0,
   };
}
