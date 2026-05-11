/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Heaps panel. Pulls per-heap info + small_heaps stats from the kernel
 * via TILCK_CMD_DP_GET_HEAPS and renders the same tabular view the
 * in-kernel modules/debugpanel/dp_heaps.c had.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>

#include <tilck/common/syscalls.h>
#include <tilck/common/dp_abi.h>

#include "term.h"
#include "tui_layout.h"
#include "dp_int.h"
#include "dp_panel.h"

#define KB_  1024UL
#define MB_  (1024UL * 1024UL)

#define MAX_DP_HEAPS  32

static struct dp_heap_info heaps[MAX_DP_HEAPS];
static struct dp_small_heaps_stats sh_stats;
static int heap_count;
static unsigned long prev_alloc[MAX_DP_HEAPS];
static unsigned long tot_usable_kb;
static unsigned long tot_used_kb;
static long tot_diff;

/* File-scope `row` for the dp_writeln macro. */
static int row;

static long dp_cmd_get_heaps(struct dp_heap_info *buf, unsigned long max,
                             struct dp_small_heaps_stats *stats)
{
   return syscall(TILCK_CMD_SYSCALL,
                  TILCK_CMD_DP_GET_HEAPS,
                  (long)buf, (long)max, (long)stats, 0L);
}

static void dp_heaps_on_enter(void)
{
   long rc = dp_cmd_get_heaps(heaps, MAX_DP_HEAPS, &sh_stats);

   if (rc < 0) {
      heap_count = 0;
      return;
   }

   heap_count = (int)rc;

   tot_usable_kb = 0;
   tot_used_kb   = 0;
   tot_diff      = 0;

   for (int i = 0; i < heap_count; i++) {

      const unsigned long size_kb = heaps[i].size / KB_;
      const unsigned long alloc_kb = heaps[i].mem_allocated / KB_;
      const long diff = (long)heaps[i].mem_allocated - (long)prev_alloc[i];

      tot_usable_kb += size_kb;
      tot_used_kb   += alloc_kb;
      tot_diff      += diff;
   }
}

static void dp_heaps_on_exit(void)
{
   for (int i = 0; i < heap_count; i++)
      prev_alloc[i] = heaps[i].mem_allocated;
}

static void dp_show_heaps(void)
{
   const int col = dp_start_col + 40;
   row = dp_screen_start_row;

   dp_writeln2("[      Small heaps      ]");
   dp_writeln2("count:    %3d [peak: %3d]",
               sh_stats.tot_count, sh_stats.peak_count);
   dp_writeln2("non-full: %3d [peak: %3d]",
               sh_stats.not_full_count, sh_stats.peak_not_full_count);

   row = dp_screen_start_row;

   if (heap_count == 0) {
      dp_writeln(E_COLOR_BR_RED
                 "TILCK_CMD_DP_GET_HEAPS failed (module not loaded?)"
                 RESET_ATTRS);
      return;
   }

   const unsigned long pct =
      tot_usable_kb ? (tot_used_kb * 100) / tot_usable_kb : 0;

   dp_writeln("Usable:  %6lu KB", tot_usable_kb);
   dp_writeln("Used:    %6lu KB (%lu%%)", tot_used_kb, pct);
   dp_writeln("Diff:   %s%s%6ld KB" RESET_ATTRS " [%ld B]",
              dp_sign_value_esc_color(tot_diff),
              tot_diff > 0 ? "+" : " ",
              tot_diff / (long)KB_,
              tot_diff);

   dp_writeln(" ");

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

   for (int i = 0; i < heap_count; i++) {

      const struct dp_heap_info *h = &heaps[i];
      char region_str[16] = "--";

      if (!h->size)
         break;

      const unsigned long size_kb = h->size / KB_;
      const unsigned long alloc_kb = h->mem_allocated / KB_;
      const long diff = (long)h->mem_allocated - (long)prev_alloc[i];

      if (h->region >= 0)
         snprintf(region_str, sizeof(region_str), "%02d", h->region);

      const unsigned long pct1k = size_kb
                                  ? (alloc_kb * 1000UL / size_kb)
                                  : 0;

      dp_writeln(
         " %2d "
         TERM_VLINE " %s "
         TERM_VLINE " 0x%08lx "
         TERM_VLINE " %3lu %s "
         TERM_VLINE " %3lu.%lu%% "
         TERM_VLINE "  %4u "
         TERM_VLINE " %s%4ld %s ",
         i, region_str,
         (unsigned long)h->vaddr,
         size_kb < 1024 ? size_kb : size_kb / 1024,
         size_kb < 1024 ? "KB" : "MB",
         pct1k / 10,
         pct1k % 10,
         h->min_block_size,
         diff > 0 ? "+" : " ",
         dp_int_abs(diff) < 4096 ? diff : diff / 1024,
         dp_int_abs(diff) < 4096 ? "B " : "KB"
      );
   }

   dp_writeln(" ");
}

static struct dp_screen dp_heaps_screen = {
   .index = 2,
   .label = "Heaps",
   .draw_func = dp_show_heaps,
   .on_dp_enter = dp_heaps_on_enter,
   .on_dp_exit = dp_heaps_on_exit,
};

__attribute__((constructor))
static void dp_heaps_register(void)
{
   dp_register_screen(&dp_heaps_screen);
}
