
#ifndef _KMALLOC_C_

   #error This is NOT a header file and it is not meant to be included

   /*
    * The only purpose of this file is to keep kmalloc.c shorter.
    * Yes, this file could be turned into a regular C source file, but at the
    * price of making several static functions and variables in kmalloc.c to be
    * just non-static. We don't want that. Code isolation is a GOOD thing.
    */

#endif

#include <exos/kernel/system_mmap.h>

#define KMALLOC_MIN_HEAP_SIZE KMALLOC_MAX_ALIGN

STATIC kmalloc_heap first_heap_struct;
STATIC kmalloc_heap *heaps[KMALLOC_HEAPS_COUNT];
STATIC int used_heaps;
STATIC char first_heap[256 * KB] __attribute__ ((aligned(KMALLOC_MAX_ALIGN)));

#include "kmalloc_leak_detector.c.h"

void *kmalloc(size_t s)
{
   ASSERT(kmalloc_initialized);

   void *ret = NULL;
   s = roundup_next_power_of_2(s);

   disable_preemption();

   // Iterate in reverse-order because the first heaps are the biggest ones.
   for (int i = used_heaps - 1; i >= 0; i--) {

      ASSERT(heaps[i] != NULL);

      const size_t heap_size = heaps[i]->size;
      const size_t heap_free = heap_size - heaps[i]->mem_allocated;

      /*
       * The heap is too small (unlikely but possible) or the heap has not been
       * created yet, therefore has size = 0 or just there is not enough free
       * space in it.
       */
      if (heap_size < s || heap_free < s)
         continue;

      void *vaddr = internal_kmalloc(heaps[i], s);

      if (vaddr) {
         s = MAX(s, heaps[i]->min_block_size);
         heaps[i]->mem_allocated += s;
         ret = vaddr;

         if (KMALLOC_SUPPORT_LEAK_DETECTOR && leak_detector_enabled) {
            debug_kmalloc_register_alloc(vaddr, s);
         }

         break;
      }
   }

   enable_preemption();
   return ret;
}

static size_t calculate_block_size(kmalloc_heap *h, uptr vaddr)
{
   block_node *nodes = h->metadata_nodes;
   int n = 0; /* root's node index */
   uptr va = h->vaddr; /* root's node data address == heap's address */
   size_t size = h->size; /* root's node size == heap's size */

   while (size > h->min_block_size) {

      if (!nodes[n].split)
         break;

      size >>= 1;

      if (vaddr >= (va + size)) {
         va += size;
         n = NODE_RIGHT(n);
      } else {
         n = NODE_LEFT(n);
      }
   }

   return size;
}

void kfree2(void *ptr, size_t user_size)
{
   const uptr vaddr = (uptr) ptr;
   size_t size;

   ASSERT(kmalloc_initialized);

   if (!ptr)
      return;

   disable_preemption();

   int hn = -1; /* the heap with the highest vaddr >= our block vaddr */

   for (int i = used_heaps - 1; i >= 0; i--) {

      uptr hva = heaps[i]->vaddr;

      if (vaddr < hva)
         continue; /* not in this heap, for sure */

      if (hn < 0 || hva > heaps[hn]->vaddr)
         hn = i;
   }

   if (hn < 0)
      goto out; /* no need to release the lock, we're going to panic */

   if (user_size) {

      size = roundup_next_power_of_2(MAX(user_size, heaps[hn]->min_block_size));

#ifdef DEBUG
      size_t cs = calculate_block_size(heaps[hn], vaddr);
      if (cs != size) {
         panic("cs[%u] != size[%u] for block at: %p\n", cs, size, vaddr);
      }
#endif

   } else {
      size = calculate_block_size(heaps[hn], vaddr);
   }

   ASSERT(vaddr >= heaps[hn]->vaddr && vaddr + size <= heaps[hn]->heap_over_end);
   internal_kfree2(heaps[hn], ptr, size);
   heaps[hn]->mem_allocated -= size;

   if (KMALLOC_FREE_MEM_POISONING) {
      memset32(ptr, KMALLOC_FREE_MEM_POISON_VAL, size / 4);
   }

   if (KMALLOC_SUPPORT_LEAK_DETECTOR && leak_detector_enabled) {
      debug_kmalloc_register_free((void *)vaddr, size);
   }

   enable_preemption();
   return;

out:
   panic("[kfree] Heap not found for block: %p\n", ptr);
}

size_t kmalloc_get_total_heap_allocation(void)
{
   size_t tot = 0;
   disable_preemption();

   for (int i = 0; i < used_heaps; i++) {
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
   // heap size has to be a multiple of KMALLOC_MIN_HEAP_SIZE
   ASSERT((size & (KMALLOC_MIN_HEAP_SIZE - 1)) == 0);

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

   h->heap_over_end = vaddr + size;
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

static void
debug_print_heap_info(uptr vaddr, u32 heap_size, u32 min_block_size)
{
   if (!heap_size) {
      printk("empty heap\n");
      return;
   }

   u32 metadata_size = calculate_heap_metadata_size(heap_size, min_block_size);

   if (heap_size >= 4 * MB)
      printk("[heap: %p] size: %u MB, "
             "min block: %u, metadata size: %u KB\n",
             vaddr, heap_size / MB, min_block_size, metadata_size / KB);
   else
      printk("[heap: %p] size: %u KB, "
             "min block: %u, metadata size: %u KB\n",
             vaddr, heap_size / KB, min_block_size, metadata_size / KB);
}

static void
debug_dump_all_heaps_info(void)
{
   for (u32 i = 0; i < ARRAY_SIZE(heaps); i++) {

      if (!heaps[i]) {

         for (u32 j = i; j < ARRAY_SIZE(heaps); j++)
            ASSERT(!heaps[j]);

         break;
      }

      debug_print_heap_info(heaps[i]->vaddr,
                            heaps[i]->size,
                            heaps[i]->min_block_size);
   }
}

void debug_kmalloc_dump_mem_usage(void)
{
   static size_t heaps_alloc[KMALLOC_HEAPS_COUNT];

   printk(NO_PREFIX "\n\nKMALLOC HEAPS: \n\n");

   printk(NO_PREFIX
          "| H# | R# |   vaddr    | size (KB) | used |   diff (B)    |\n");

   printk(NO_PREFIX
          "+----+----+------------+-----------+------+---------------+\n");

   for (u32 i = 0; i < ARRAY_SIZE(heaps) && heaps[i]; i++) {

      char region_str[8] = "--";

      ASSERT(heaps[i]->size);
      uptr size_kb = heaps[i]->size / KB;
      uptr allocated_kb = heaps[i]->mem_allocated / KB;

      if (heaps[i]->region >= 0)
         snprintk(region_str, sizeof(region_str), "%02d", heaps[i]->region);

      printk(NO_PREFIX "| %2d | %s | %p |  %6u   |  %2u%% | %9d     |\n",
             i, region_str,
             heaps[i]->vaddr,
             size_kb,
             allocated_kb * 100 / size_kb,
             heaps[i]->mem_allocated - heaps_alloc[i]);
   }

   printk(NO_PREFIX "\n");

   for (u32 i = 0; i < ARRAY_SIZE(heaps) && heaps[i]; i++) {
      heaps_alloc[i] = heaps[i]->mem_allocated;
   }
}

static int kmalloc_internal_add_heap(void *vaddr, size_t heap_size)
{
   const size_t metadata_size = heap_size / 32;
   size_t min_block_size;

   if (used_heaps >= (int)ARRAY_SIZE(heaps))
      return -1;

   min_block_size = calculate_heap_min_block_size(heap_size, metadata_size);

   if (!used_heaps) {

      heaps[used_heaps] = &first_heap_struct;

   } else {

      heaps[used_heaps] = kmalloc(sizeof(kmalloc_heap));

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
    * allocation using internal_kmalloc().
    */

   void *md_allocated = internal_kmalloc(heaps[used_heaps], metadata_size);

   /*
    * We have to be SURE that the allocation returned the very beginning of
    * the heap, as we expected.
    */

   VERIFY(md_allocated == vaddr);
   return used_heaps++;
}

static int greater_than_heap_cmp(const void *a, const void *b)
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
   int heap_index;
   ASSERT(!kmalloc_initialized);

   used_heaps = 0;
   bzero(heaps, sizeof(heaps));

   heap_index = kmalloc_internal_add_heap(&first_heap, sizeof(first_heap));
   VERIFY(heap_index == 0);

   heaps[heap_index]->region =
      system_mmap_get_region_of(KERNEL_VA_TO_PA(&first_heap));

   kmalloc_initialized = true; /* we have at least 1 heap */

   for (int i = 0; i < mem_regions_count; i++) {

      memory_region_t *r = mem_regions + i;
      uptr vbegin, vend;

      if (!linear_map_mem_region(r, &vbegin, &vend))
         break;

      if (r->type == MULTIBOOT_MEMORY_AVAILABLE) {

         init_kmalloc_fill_region(i, vbegin, vend);

         if (vend == LINEAR_MAPPING_OVER_END)
            break;
      }
   }

   insertion_sort_ptr(heaps,
                      used_heaps,
                      greater_than_heap_cmp);

#if KMALLOC_HEAPS_CREATION_DEBUG
   debug_dump_all_heaps_info();
#endif
}

