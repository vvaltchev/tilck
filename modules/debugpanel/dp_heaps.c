/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/kmalloc_debug.h>

#include "termutil.h"
#include "dp_int.h"

static size_t heaps_alloc[KMALLOC_HEAPS_COUNT];
static struct debug_kmalloc_heap_info hi;
static struct debug_kmalloc_stats stats;
static size_t tot_usable_mem_kb;
static size_t tot_used_mem_kb;
static long tot_diff;

static void dp_heaps_on_enter(void)
{
   tot_usable_mem_kb = 0;
   tot_used_mem_kb = 0;
   tot_diff = 0;

   for (int i = 0; i < KMALLOC_HEAPS_COUNT; i++) {

      if (!debug_kmalloc_get_heap_info(i, &hi))
         break;

      const uptr size_kb = hi.size / KB;
      const uptr allocated_kb = hi.mem_allocated / KB;
      const long diff = (long)hi.mem_allocated - (long)heaps_alloc[i];

      tot_usable_mem_kb += size_kb;
      tot_used_mem_kb += allocated_kb;
      tot_diff += diff;
   }

   // SA: avoid division by zero warning
   ASSERT(tot_usable_mem_kb > 0);

   debug_kmalloc_get_stats(&stats);
}

static void dp_show_kmalloc_heaps(void)
{
   int row = dp_screen_start_row;
   const int col = dp_start_col + 40;

   dp_writeln2("[      Small heaps      ]");
   dp_writeln2("count:    %3d [peak: %3d]",
               stats.small_heaps.tot_count,
               stats.small_heaps.peak_count);
   dp_writeln2("non-full: %3d [peak: %3d]",
               stats.small_heaps.not_full_count,
               stats.small_heaps.peak_not_full_count);

   row = dp_screen_start_row;

   dp_writeln("Usable:  %6u KB", tot_usable_mem_kb);
   dp_writeln("Used:    %6u KB (%u%%)",
              tot_used_mem_kb, (tot_used_mem_kb * 100) / tot_usable_mem_kb);
   dp_writeln("Diff:   %s%s%6d KB" RESET_ATTRS " [%d B]",
              dp_sign_value_esc_color(tot_diff),
              tot_diff > 0 ? "+" : " ",
              tot_diff / (long)KB,
              tot_diff);

   dp_writeln("");

   dp_writeln(
      " H# "
      TERM_VLINE " R# "
      TERM_VLINE "   vaddr    "
      TERM_VLINE "  size  "
      TERM_VLINE "  used  "
      TERM_VLINE "  MBS  "
      TERM_VLINE "   diff   "
   );

   dp_writeln(
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
      const long diff = (long)hi.mem_allocated - (long)heaps_alloc[i];

      if (hi.region >= 0)
         snprintk(region_str, sizeof(region_str), "%02d", hi.region);

      dp_writeln(
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
   }

   dp_writeln("");
}

static void dp_heaps_on_exit(void)
{
   for (int i = 0; i < KMALLOC_HEAPS_COUNT; i++) {

      if (!debug_kmalloc_get_heap_info(i, &hi))
         break;

      heaps_alloc[i] = hi.mem_allocated;
   }
}

static struct dp_screen dp_heaps_screen =
{
   .index = 2,
   .label = "Heaps",
   .draw_func = dp_show_kmalloc_heaps,
   .on_keypress_func = NULL,
   .on_dp_enter = dp_heaps_on_enter,
   .on_dp_exit = dp_heaps_on_exit,
};

__attribute__((constructor))
static void dp_heaps_init(void)
{
   dp_register_screen(&dp_heaps_screen);
}
