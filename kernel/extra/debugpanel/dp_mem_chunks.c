/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/sched.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/cmdline.h>

#include "termutil.h"
#include "dp_int.h"

typedef struct {
   size_t size;
   size_t count;
   u64 max_waste;
} chunk_info;

static debug_kmalloc_stats stats;
static u64 lf_allocs;
static u64 lf_waste;
static size_t chunks_count;
static chunk_info chunks_arr[512];

static void dp_chunks_enter(void)
{
   debug_kmalloc_chunks_ctx ctx;
   size_t s, c;

   if (!KMALLOC_HEAVY_STATS)
      return;

   debug_kmalloc_get_stats(&stats);
   lf_allocs = 0;
   lf_waste = 0;
   chunks_count = 0;

   disable_preemption();
   {
      debug_kmalloc_chunks_stats_start_read(&ctx);
      while (debug_kmalloc_chunks_stats_next(&ctx, &s, &c)) {

         if (chunks_count == ARRAY_SIZE(chunks_arr))
            break;

         const u64 waste = (u64)(roundup_next_power_of_2(s) - s) * c;

         chunks_arr[chunks_count++] = (chunk_info) {
            .size = s,
            .count = c,
            .max_waste = waste,
         };

         lf_allocs += (u64)s * c;
         lf_waste += waste;
      }
   }
   enable_preemption();
}

static void dp_chunks_exit(void)
{
   if (!KMALLOC_HEAVY_STATS)
      return;
}

static void dp_show_chunks(void)
{
   int row = dp_screen_start_row;
   const u64 lf_tot = lf_allocs + lf_waste;

   if (!KMALLOC_HEAVY_STATS) {
      dp_writeln("Not available: recompile with KMALLOC_HEAVY_STATS=1");
      return;
   }

   dp_writeln("Chunk sizes count:         %5u sizes", chunks_count);
   dp_writeln("Lifetime data allocated:   %5llu %s [actual: %llu %s]",
              lf_allocs < 32*MB ? lf_allocs/KB : lf_allocs/MB,
              lf_allocs < 32*MB ? "KB" : "MB",
              lf_tot < 32*MB ? lf_tot/KB : lf_tot/MB,
              lf_tot < 32*MB ? "KB" : "MB");
   dp_writeln("Lifetime max data waste:   %5llu %s (%llu.%llu%%)",
              lf_waste < 32*MB ? lf_waste/KB : lf_waste/MB,
              lf_waste < 32*MB ? "KB" : "MB",
              lf_waste * 100 / lf_tot,
              (lf_waste * 1000 / lf_tot) % 10);


   dp_writeln("");

   dp_writeln(
      "   Size   "
      TERM_VLINE "  Count  "
      TERM_VLINE " Max waste "
      TERM_VLINE " Max waste (%%)"
   );

   dp_writeln(
      GFX_ON
      "qqqqqqqqqqnqqqqqqqqqnqqqqqqqqqqqnqqqqqqqqqqqqqqqqqq"
      GFX_OFF
   );

   for (size_t i = 0; i < chunks_count; i++) {

      const u64 tot = (u64)chunks_arr[i].size * chunks_arr[i].count;
      const u64 waste = chunks_arr[i].max_waste;

      dp_writeln("%9u "
                 TERM_VLINE " %7u "
                 TERM_VLINE " %6llu %s "
                 TERM_VLINE " %6llu.%llu%%",
                 chunks_arr[i].size,
                 chunks_arr[i].count,
                 waste < KB ? waste : waste / KB,
                 waste < KB ? "B " : "KB",
                 waste * 100 / (waste + tot),
                 (waste * 1000 / (waste + tot)) % 10);
   }

   dp_writeln("");
}

static dp_screen dp_chunks_screen =
{
   .index = 5,
   .label = "MemChunks",
   .draw_func = dp_show_chunks,
   .on_dp_enter = dp_chunks_enter,
   .on_dp_exit = dp_chunks_exit,
   .on_keypress_func = NULL,
};

__attribute__((constructor))
static void dp_chunks_init(void)
{
   dp_register_screen(&dp_chunks_screen);
}
