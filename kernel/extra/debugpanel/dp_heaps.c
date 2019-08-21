/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/term.h>

#include "termutil.h"
#include "dp_int.h"

static void dp_show_kmalloc_heaps(void)
{
   static size_t heaps_alloc[KMALLOC_HEAPS_COUNT];
   static debug_kmalloc_heap_info hi;
   static debug_kmalloc_stats stats;

   size_t tot_usable_mem_kb = 0;
   size_t tot_used_mem_kb = 0;
   sptr tot_diff = 0;
   const int row = term_get_curr_row(get_curr_term()) + 1;

   for (int i = 0; i < KMALLOC_HEAPS_COUNT; i++) {

      if (!debug_kmalloc_get_heap_info(i, &hi))
         break;

      const uptr size_kb = hi.size / KB;
      const uptr allocated_kb = hi.mem_allocated / KB;
      const sptr diff = (sptr)hi.mem_allocated - (sptr)heaps_alloc[i];

      tot_usable_mem_kb += size_kb;
      tot_used_mem_kb += allocated_kb;
      tot_diff += diff;
   }


   // SA: avoid division by zero warning
   ASSERT(tot_usable_mem_kb > 0);

   debug_kmalloc_get_stats(&stats);

   dp_move_cursor(row, dp_start_col + 40);
   dp_printk("[     Small heaps     ]");
   dp_move_cursor(row + 1, dp_start_col + 40);
   dp_printk("count:    %2d [peak: %2d]",
             stats.small_heaps.tot_count, stats.small_heaps.peak_count);
   dp_move_cursor(row + 2, dp_start_col + 40);
   dp_printk("non-full: %2d [peak: %2d]",
             stats.small_heaps.not_full_count,
             stats.small_heaps.peak_not_full_count);

   dp_move_cursor(row, 1);
   dp_printkln("Usable:  %6u KB", tot_usable_mem_kb);
   dp_printkln("Used:    %6u KB (%u%%)",
               tot_used_mem_kb, (tot_used_mem_kb * 100) / tot_usable_mem_kb);
   dp_printkln("Diff:   %s%s%6d KB" RESET_ATTRS " [%d B]",
               dp_sign_value_esc_color(tot_diff),
               tot_diff > 0 ? "+" : " ",
               tot_diff / (sptr)KB,
               tot_diff);

   dp_printkln("");
   dp_printkln(
      " H# "
      TERM_VLINE " R# "
      TERM_VLINE "   vaddr    "
      TERM_VLINE "  size  "
      TERM_VLINE "  used  "
      TERM_VLINE "  MBS  "
      TERM_VLINE "   diff   "
   );

   dp_printkln(
      GFX_ON
      "qqqqnqqqqnqqqqqqqqqqqqnqqqqqqqqnqqqqqqqqnqqqqqqqnqqqqqqqqqq"
      GFX_OFF
   );

   for (int i = 0; i < KMALLOC_HEAPS_COUNT; i++) {

      if (!debug_kmalloc_get_heap_info(i, &hi))
         break;

      char region_str[8] = "--";

      ASSERT(hi.size);
      const uptr size_kb = hi.size / KB;
      const uptr allocated_kb = hi.mem_allocated / KB;
      const sptr diff = (sptr)hi.mem_allocated - (sptr)heaps_alloc[i];

      if (hi.region >= 0)
         snprintk(region_str, sizeof(region_str), "%02d", hi.region);

      dp_printkln(
         " %2d "
         TERM_VLINE " %s "
         TERM_VLINE " %p "
         TERM_VLINE " %3u %s "
         TERM_VLINE " %3u.%u%% "
         TERM_VLINE "  %4d "
         TERM_VLINE " %s%4d %s ",
         i, region_str,
         hi.vaddr,
         size_kb < 1024 ? size_kb : size_kb / 1024,
         size_kb < 1024 ? "KB" : "MB",
         allocated_kb * 100 / size_kb,
         (allocated_kb * 1000 / size_kb) % 10,
         hi.min_block_size,
         diff > 0 ? "+" : " ",
         dp_int_abs(diff) < 4096 ? diff : diff / 1024,
         dp_int_abs(diff) < 4096 ? "B " : "KB"
      );

      heaps_alloc[i] = hi.mem_allocated;
   }
}

static dp_screen dp_heaps_screen =
{
   .index = 2,
   .label = "Heaps",
   .draw_func = dp_show_kmalloc_heaps,
   .on_keypress_func = NULL,
};

__attribute__((constructor))
static void dp_heaps_init(void)
{
   dp_register_screen(&dp_heaps_screen);
}
