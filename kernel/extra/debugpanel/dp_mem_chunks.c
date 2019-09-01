/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/cmdline.h>

#include "termutil.h"
#include "dp_int.h"

static debug_kmalloc_chunk_stat chunks_arr[1024];

static void dp_show_chunks(void)
{
   debug_kmalloc_stats stats;
   int row = dp_screen_start_row;

   if (!KMALLOC_HEAVY_STATS) {
      dp_writeln("Not available: recompile with KMALLOC_HEAVY_STATS=1");
      return;
   }

   debug_kmalloc_get_stats(&stats);

   if (stats.chunk_sizes_count > ARRAY_SIZE(chunks_arr)) {
      dp_writeln("Not enough space in chunks_arr!");
      return;
   }

   debug_kmalloc_get_chunks_info(chunks_arr);

   for (size_t i = 0; i < stats.chunk_sizes_count; i++) {
      dp_writeln("Size: %8u -> %6u allocs",
                 chunks_arr[i].size, chunks_arr[i].count);
   }
}

static dp_screen dp_chunks_screen =
{
   .index = 5,
   .label = "MemChunks",
   .draw_func = dp_show_chunks,
   .on_keypress_func = NULL,
};

__attribute__((constructor))
static void dp_chunks_init(void)
{
   dp_register_screen(&dp_chunks_screen);
}
