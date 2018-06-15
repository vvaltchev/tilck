
#ifndef _KMALLOC_C_

   #error This is NOT a header file and it is not meant to be included

   /*
    * The only purpose of this file is to keep kmalloc.c shorter.
    * Yes, this file could be turned into a regular C source file, but at the
    * price of making several static functions and variables in kmalloc.c to be
    * just non-static. We don't want that. Code isolation is a GOOD thing.
    */

#endif

#if defined(__i386__) || defined(__x86_64__)
#define X86_LOW_MEM_UPPER_LIM 0xA0000 // + 640 KB [arch-limit, don't touch!]

/*
 * NOTE how all of below defines depend somehow on:
 *
 *    - KMALLOC_LOW_MEM_UPPER_LIM
 *    - KMALLOC_MAX_ALIGN
 *
 * In summary:
 *
 *    - We cannot get a bigger MAX ALIGN than LOW_MEM_HEAP_PA, unless we avoid
 *      using the low mem, but that would mean wasting 512 KB.
 *      In alternative, we have to tollerate different heaps to have a different
 *      max align value. For the moment, that is not necessary.
 *
 *    - LOW_MEM_HEAP_SIZE cannot be more than 512 KB, since:
 *          - it has to be a power of 2 (limitation by design of the allocator)
 *          - it is limited by X86_LOW_MEM_UPPER_LIM (arch-specific limit)
 */

#define LOW_MEM_HEAP_PA (KMALLOC_MAX_ALIGN)
#define LOW_MEM_HEAP (KERNEL_PA_TO_VA(LOW_MEM_HEAP_PA))
#define LOW_MEM_HEAP_SIZE (512 * KB)

STATIC_ASSERT(LOW_MEM_HEAP_PA + LOW_MEM_HEAP_SIZE <= X86_LOW_MEM_UPPER_LIM);

#endif

#define KMALLOC_MIN_HEAP_SIZE KMALLOC_MAX_ALIGN

STATIC kmalloc_heap heaps[KMALLOC_HEAPS_COUNT];
STATIC int used_heaps;
STATIC char first_heap[256 * KB] __attribute__ ((aligned(KMALLOC_MAX_ALIGN)));

bool pg_alloc_and_map(uptr vaddr, int page_count);
void pg_free_and_unmap(uptr vaddr, int page_count);

#include "kmalloc_leak_detector.c.h"

void *kmalloc(size_t s)
{
   ASSERT(kmalloc_initialized);

   void *ret = NULL;
   s = roundup_next_power_of_2(s);

   disable_preemption();

   // Iterate in reverse-order because the first heaps are the biggest ones.
   for (int i = used_heaps - 1; i >= 0; i--) {

      const size_t heap_size = heaps[i].size;
      const size_t heap_free = heap_size - heaps[i].mem_allocated;

      /*
       * The heap is too small (unlikely but possible) or the heap has not been
       * created yet, therefore has size = 0 or just there is not enough free
       * space in it.
       */
      if (heap_size < s || heap_free < s)
         continue;

      void *vaddr = internal_kmalloc(&heaps[i], s);

      if (vaddr) {
         s = MAX(s, heaps[i].min_block_size);
         heaps[i].mem_allocated += s;
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

      uptr hva = heaps[i].vaddr;

      if (vaddr < hva)
         continue; /* not in this heap, for sure */

      if (hn < 0 || hva > heaps[hn].vaddr)
         hn = i;
   }

   if (hn < 0)
      goto out; /* no need to release the lock, we're going to panic */

   if (user_size) {

      size = roundup_next_power_of_2(MAX(user_size, heaps[hn].min_block_size));

#ifdef DEBUG
      size_t cs = calculate_block_size(&heaps[hn], vaddr);
      if (cs != size) {
         panic("cs[%u] != size[%u] for block at: %p\n", cs, size, vaddr);
      }
#endif

   } else {
      size = calculate_block_size(&heaps[hn], vaddr);
   }

   ASSERT(vaddr >= heaps[hn].vaddr && vaddr + size <= heaps[hn].heap_over_end);
   internal_kfree2(&heaps[hn], ptr, size);
   heaps[hn].mem_allocated -= size;

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
      tot += heaps[i].mem_allocated;
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

   h->valloc_and_map = valloc ? valloc : pg_alloc_and_map;
   h->vfree_and_unmap = vfree ? vfree : pg_free_and_unmap;

   if (!metadata_nodes) {
      // It is OK to pass NULL as 'metadata_nodes' if at least one heap exists.
      ASSERT(heaps[0].vaddr != 0);

      metadata_nodes = kmalloc(h->metadata_size);

      if (!metadata_nodes)
         return false;
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
   size_t curr_max = 512 * MB;

   while (curr_max) {

      if (vaddr + curr_max <= limit)
         break;

      curr_max >>= 1; // divide by 2.
   }

   return curr_max;
}

extern uptr ramdisk_paddr;
extern size_t ramdisk_size;

static void
debug_print_heap_info(uptr vaddr, u32 heap_size, u32 min_block_size)
{
#if KMALLOC_HEAPS_CREATION_DEBUG

   u32 metadata_size = calculate_heap_metadata_size(heap_size, min_block_size);

   if (heap_size >= 4 * MB)
      printk("[heap: %p] size: %u MB, "
             "min block: %u, metadata size: %u KB\n",
             vaddr, heap_size / MB, min_block_size, metadata_size / KB);
   else
      printk("[heap: %p] size: %u KB, "
             "min block: %u, metadata size: %u KB\n",
             vaddr, heap_size / KB, min_block_size, metadata_size / KB);

#endif
}

void debug_kmalloc_dump_mem_usage(void)
{
   static size_t heaps_alloc[KMALLOC_HEAPS_COUNT];

   printk("\n-------------------- kmalloc heaps --------------------\n");

   for (u32 i = 0; i < ARRAY_SIZE(heaps); i++) {

      if (!heaps[i].size)
         break;

      uptr size_kb = heaps[i].size / KB;
      uptr allocated_kb = heaps[i].mem_allocated / KB;

      printk("[heap %d] size: %u KB, allocated: %u KB [%u %%], diff: %d B\n",
             i, size_kb, allocated_kb, allocated_kb * 100 / size_kb,
             heaps[i].mem_allocated - heaps_alloc[i]);
   }

   for (u32 i = 0; i < ARRAY_SIZE(heaps); i++) {
      heaps_alloc[i] = heaps[i].mem_allocated;
   }
}

static int kmalloc_internal_add_heap(void *vaddr, size_t heap_size)
{
   const size_t metadata_size = heap_size / 32;
   size_t min_block_size;

   if (used_heaps >= (int)ARRAY_SIZE(heaps))
      return -1;

   min_block_size = calculate_heap_min_block_size(heap_size, metadata_size);
   debug_print_heap_info((uptr)vaddr, heap_size, min_block_size);

   bool success =
      kmalloc_create_heap(&heaps[used_heaps],
                          (uptr)vaddr,
                          heap_size,
                          min_block_size,
                          0,              /* alloc_block_size */
                          true,           /* linear mapping */
                          vaddr,          /* metadata_nodes */
                          NULL, NULL);

   VERIFY(success);

   /*
    * We passed to kmalloc_create_heap() the begining of the heap as 'metadata'
    * in order to avoid using another heap (that might not be large enough) for
    * that. Now we MUST register that area in the metadata itself, by doing an
    * allocation using internal_kmalloc().
    */

   void *md_allocated = internal_kmalloc(&heaps[used_heaps], metadata_size);

   /*
    * We have to be SURE that the allocation returned the very beginning of
    * the heap, as we expected.
    */

   VERIFY(md_allocated == vaddr);
   return used_heaps++;
}

void init_kmalloc(void)
{
   int heap_index;
   uptr vaddr;

   ASSERT(!kmalloc_initialized);

   used_heaps = 0;
   heap_index = kmalloc_internal_add_heap(&first_heap, sizeof(first_heap));
   VERIFY(heap_index == 0);

   kmalloc_initialized = true; /* we have at least 1 heap */

   const uptr limit =
      KERNEL_BASE_VA + MIN(get_phys_mem_mb(), LINEAR_MAPPING_MB) * MB;

   const uptr base_vaddr =
      ramdisk_size
         ? (uptr)KERNEL_PA_TO_VA(ramdisk_paddr) + ramdisk_size
         : (uptr)KERNEL_PA_TO_VA(KERNEL_PADDR) + KERNEL_MAX_SIZE;

   vaddr = round_up_at(base_vaddr, KMALLOC_MAX_ALIGN);

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

      vaddr = heaps[heap_index].vaddr + heaps[heap_index].size;
   }

#if defined(__i386__) || defined(__x86_64__)

   heap_index =
      kmalloc_internal_add_heap((void *)LOW_MEM_HEAP, LOW_MEM_HEAP_SIZE);

   if (heap_index < 0) {
      printk("kmalloc: no heap slot for the low-mem heap\n");
   }

#endif
}

