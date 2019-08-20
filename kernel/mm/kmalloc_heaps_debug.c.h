/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

static void
debug_print_heap_info(uptr vaddr, size_t heap_size, size_t min_block_size)
{
   if (!heap_size) {
      printk("empty heap\n");
      return;
   }

   uptr metadata_size = calculate_heap_metadata_size(heap_size, min_block_size);

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

   size_t tot_usable_mem_kb = 0;
   size_t tot_used_mem_kb = 0;

   printk(NO_PREFIX
      "| H# | R# |   vaddr    | size (KB) | used |  MBS  |   diff (B)    |\n");

   printk(NO_PREFIX
      "+----+----+------------+-----------+------+-------+---------------+\n");

   for (u32 i = 0; i < ARRAY_SIZE(heaps) && heaps[i]; i++) {

      char region_str[8] = "--";

      ASSERT(heaps[i]->size);
      uptr size_kb = heaps[i]->size / KB;
      uptr allocated_kb = heaps[i]->mem_allocated / KB;

      if (heaps[i]->region >= 0)
         snprintk(region_str, sizeof(region_str), "%02d", heaps[i]->region);

      printk(NO_PREFIX "| %2d | %s | %p |  %6u   | %3u%% | %4d  | %9d     |\n",
             i, region_str,
             heaps[i]->vaddr,
             size_kb,
             allocated_kb * 100 / size_kb,
             heaps[i]->min_block_size,
             heaps[i]->mem_allocated - heaps_alloc[i]);

      tot_usable_mem_kb += size_kb;
      tot_used_mem_kb += allocated_kb;
   }

   // SA: avoid division by zero warning
   ASSERT(tot_usable_mem_kb > 0);

   printk(NO_PREFIX "\n");
   printk(NO_PREFIX "Total usable: %6u KB\n", tot_usable_mem_kb);
   printk(NO_PREFIX "Total used:   %6u KB (%u%%)\n\n",
          tot_used_mem_kb, (tot_used_mem_kb * 100) / tot_usable_mem_kb);

   printk(NO_PREFIX
          "Small heaps: count: %d [peak: %d], non-full: %d [peak: %d]\n\n",
          total_small_heaps_count,
          peak_small_heaps_count,
          not_full_small_heaps_count,
          peak_non_full_small_heaps_count);

   for (u32 i = 0; i < ARRAY_SIZE(heaps) && heaps[i]; i++) {
      heaps_alloc[i] = heaps[i]->mem_allocated;
   }
}
