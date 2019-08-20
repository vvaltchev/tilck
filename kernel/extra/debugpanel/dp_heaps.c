/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/kernel/kmalloc.h>

#include "termutil.h"
#include "dp_int.h"

void dp_show_kmalloc_heaps(void)
{
   static size_t heaps_alloc[KMALLOC_HEAPS_COUNT];
   static debug_kmalloc_heap_info hi;
   static debug_kmalloc_stats stats;

   size_t tot_usable_mem_kb = 0;
   size_t tot_used_mem_kb = 0;
   sptr tot_diff = 0;

   dp_printk(
      "  "
      "| H# | R# |   vaddr    | size (KB) | used |  MBS  |   diff (B)    |\n");

   dp_printk(
      "  "
      "+----+----+------------+-----------+------+-------+---------------+\n");

   for (int i = 0; i < KMALLOC_HEAPS_COUNT; i++) {

      if (!debug_kmalloc_get_heap_info(i, &hi))
         break;

      char region_str[8] = "--";

      ASSERT(hi.size);
      uptr size_kb = hi.size / KB;
      uptr allocated_kb = hi.mem_allocated / KB;

      if (hi.region >= 0)
         snprintk(region_str, sizeof(region_str), "%02d", hi.region);

      dp_printk("  | %2d | %s | %p |  %6u   | %3u%% | %4d  | %9d     |\n",
                i, region_str,
                hi.vaddr,
                size_kb,
                allocated_kb * 100 / size_kb,
                hi.min_block_size,
                hi.mem_allocated - heaps_alloc[i]);

      tot_usable_mem_kb += size_kb;
      tot_used_mem_kb += allocated_kb;
      tot_diff += hi.mem_allocated - heaps_alloc[i];
      heaps_alloc[i] = hi.mem_allocated;
   }

   // SA: avoid division by zero warning
   ASSERT(tot_usable_mem_kb > 0);

   dp_printk("\n");
   dp_printk("  Total usable:  %6u KB\n", tot_usable_mem_kb);
   dp_printk("  Total used:    %6u KB (%u%%)\n",
             tot_used_mem_kb, (tot_used_mem_kb * 100) / tot_usable_mem_kb);
   dp_printk("  Total diff:    %6d KB [%d B]\n\n",
             tot_diff / (sptr)KB, tot_diff);

   debug_kmalloc_get_stats(&stats);

   dp_printk("  Small heaps: count: %d [peak: %d], non-full: %d [peak: %d]\n",
             stats.small_heaps.tot_count,
             stats.small_heaps.peak_count,
             stats.small_heaps.not_full_count,
             stats.small_heaps.peak_not_full_count);
}
