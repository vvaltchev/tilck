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

   dp_printkln(
      "| H# | R# |   vaddr    | size (KB) | used |  MBS  |   diff (B)    |");

   dp_printkln(
      "+----+----+------------+-----------+------+-------+---------------+");

   for (int i = 0; i < KMALLOC_HEAPS_COUNT; i++) {

      if (!debug_kmalloc_get_heap_info(i, &hi))
         break;

      char region_str[8] = "--";

      ASSERT(hi.size);
      uptr size_kb = hi.size / KB;
      uptr allocated_kb = hi.mem_allocated / KB;

      if (hi.region >= 0)
         snprintk(region_str, sizeof(region_str), "%02d", hi.region);

      dp_printkln("| %2d | %s | %p |  %6u   | %3u%% | %4d  | %9d     |",
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

   debug_kmalloc_get_stats(&stats);

   dp_printkln("");
   dp_printkln("Total usable:  %6u KB", tot_usable_mem_kb);
   dp_printkln("Total used:    %6u KB (%u%%)",
               tot_used_mem_kb, (tot_used_mem_kb * 100) / tot_usable_mem_kb);
   dp_printkln("Total diff:    %6d KB [%d B]",
               tot_diff / (sptr)KB, tot_diff);

   dp_printkln("Small heaps: count: %d [peak: %d], non-full: %d [peak: %d]",
               stats.small_heaps.tot_count,
               stats.small_heaps.peak_count,
               stats.small_heaps.not_full_count,
               stats.small_heaps.peak_not_full_count);
}
