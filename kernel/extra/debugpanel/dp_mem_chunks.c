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
   size_t max_waste;
} chunk_info;

static debug_kmalloc_stats stats;
static u64 lifetime_allocs;
static u64 lifetime_waste;
static size_t chunks_count;
static chunk_info chunks_arr[512];

static void dp_chunks_enter(void)
{
   debug_kmalloc_chunks_ctx ctx;
   size_t s, c;

   if (!KMALLOC_HEAVY_STATS)
      return;

   debug_kmalloc_get_stats(&stats);
   lifetime_allocs = 0;
   lifetime_waste = 0;
   chunks_count = 0;

   disable_preemption();
   {
      debug_kmalloc_chunks_stats_start_read(&ctx);
      while (debug_kmalloc_chunks_stats_next(&ctx, &s, &c)) {

         if (chunks_count == ARRAY_SIZE(chunks_arr))
            break;

         const size_t waste = (roundup_next_power_of_2(s) - s) * c;

         chunks_arr[chunks_count++] = (chunk_info) {
            .size = s,
            .count = c,
            .max_waste = waste,
         };

         lifetime_allocs += s;
         lifetime_waste += waste;
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

   if (!KMALLOC_HEAVY_STATS) {
      dp_writeln("Not available: recompile with KMALLOC_HEAVY_STATS=1");
      return;
   }

   dp_writeln("Chunk sizes count:         %5u sizes", chunks_count);
   dp_writeln("Lifetime data allocated:   %5llu %s",
              lifetime_allocs < 32*MB ? lifetime_allocs/KB : lifetime_allocs/MB,
              lifetime_allocs < 32*MB ? "KB" : "MB");
   dp_writeln("Lifetime max data waste:   %5llu %s (%llu.%llu%%)",
              lifetime_waste < 32*MB ? lifetime_waste/KB : lifetime_waste/MB,
              lifetime_waste < 32*MB ? "KB" : "MB",
              lifetime_waste * 100 / lifetime_allocs,
              (lifetime_waste * 1000 / lifetime_allocs) % 10);


   dp_writeln("");

   dp_writeln(
      "   Size   "
      TERM_VLINE "  Count  "
      TERM_VLINE " Max waste "
   );

   dp_writeln(
      GFX_ON
      "qqqqqqqqqqnqqqqqqqqqnqqqqqqqqqqq"
      GFX_OFF
   );

   for (size_t i = 0; i < chunks_count; i++) {

      const size_t max_waste = chunks_arr[i].max_waste;

      dp_writeln("%9u " TERM_VLINE " %7u " TERM_VLINE " %6u %s ",
                 chunks_arr[i].size,
                 chunks_arr[i].count,
                 max_waste < KB
                  ? max_waste
                  : max_waste / KB,
                 max_waste < KB
                  ? "B "
                  : "KB");
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
