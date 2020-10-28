/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_debug.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/kb.h>
#include <tilck/kernel/sched.h>

#include "termutil.h"

static int row;

static void debug_dump_slow_irq_handler_count(void)
{
   extern u32 slow_timer_irq_handler_count;

   if (KRN_TRACK_NESTED_INTERR) {
      dp_writeln("   Slow timer irq handler counter: %u",
                 slow_timer_irq_handler_count);
   }
}

static void debug_dump_spur_irq_count(void)
{
   extern u32 spur_irq_count;
   const u64 ticks = get_ticks();

   if (ticks > TIMER_HZ)
      dp_writeln("   Spurious IRQ count: %u (%u/sec)",
                 spur_irq_count,
                 spur_irq_count / (ticks / TIMER_HZ));
   else
      dp_writeln("   Spurious IRQ count: %u", spur_irq_count);
}

static void debug_dump_unhandled_irq_count(void)
{
   extern u32 unhandled_irq_count[256];
   u32 tot_count = 0;

   for (int i = 0; i < ARRAY_SIZE(unhandled_irq_count); i++)
      tot_count += unhandled_irq_count[i];

   if (!tot_count)
      return;

   dp_writeln("");
   dp_writeln("Unhandled IRQs count table");

   for (int i = 0; i < ARRAY_SIZE(unhandled_irq_count); i++) {

      if (unhandled_irq_count[i])
         dp_writeln("   IRQ #%3u: %3u unhandled", i,
                    unhandled_irq_count[i]);
   }
}

static void debug_dump_masked_irqs(void)
{
   dp_writeln("");
   dp_writeln("Unmasked IRQs: ");

   for (int i = 0; i < 16; i++) {
      if (!irq_is_masked(i))
         dp_write_raw("#%u ", i);
   }

   dp_writeln("");
}

static void dp_show_irq_stats(void)
{
   row = dp_screen_start_row;

   dp_writeln("Kernel IRQ-related counters");
   debug_dump_slow_irq_handler_count();
   debug_dump_spur_irq_count();
   debug_dump_unhandled_irq_count();
   debug_dump_masked_irqs();
}

static struct dp_screen dp_irqs_screen =
{
   .index = 4,
   .label = "IRQs",
   .draw_func = dp_show_irq_stats,
   .on_keypress_func = NULL,
};

__attribute__((constructor))
static void dp_irqs_init(void)
{
   dp_register_screen(&dp_irqs_screen);
}
