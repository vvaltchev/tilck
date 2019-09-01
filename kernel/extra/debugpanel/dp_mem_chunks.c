/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/cmdline.h>

#include "termutil.h"
#include "dp_int.h"

typedef struct {
   size_t size;
   size_t count;
} chunk_info;

static debug_kmalloc_stats stats;
static size_t chunks_count;
static chunk_info chunks_arr[1024];

static void dp_chunks_enter(void)
{
   debug_kmalloc_chunks_ctx ctx;
   size_t s, c;

   if (!KMALLOC_HEAVY_STATS)
      return;

   debug_kmalloc_get_stats(&stats);
   chunks_count = 0;

   disable_preemption();
   {
      debug_kmalloc_chunks_stats_start_read(&ctx);
      while (debug_kmalloc_chunks_stats_next(&ctx, &s, &c)) {

         if (chunks_count == ARRAY_SIZE(chunks_arr))
            break;

         chunks_arr[chunks_count++] = (chunk_info) {
            .size = s,
            .count = c,
         };
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

   dp_writeln("Total number of different chunk sizes: %u", chunks_count);
   dp_writeln("");

   for (size_t i = 0; i < chunks_count; i++) {
      dp_writeln("Size: %8u -> %6u allocs",
                 chunks_arr[i].size, chunks_arr[i].count);
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
