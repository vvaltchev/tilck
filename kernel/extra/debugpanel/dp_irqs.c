/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/kb.h>
#include <tilck/kernel/system_mmap.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/tty.h>
#include <tilck/kernel/fb_console.h>
#include <tilck/kernel/cmdline.h>

#include "termutil.h"

static void debug_dump_slow_irq_handler_count(void)
{
   extern u32 slow_timer_irq_handler_count;

   if (KERNEL_TRACK_NESTED_INTERRUPTS) {
      dp_printk("   Slow timer irq handler counter: %u\n",
                slow_timer_irq_handler_count);
   }
}

static void debug_dump_spur_irq_count(void)
{
   extern u32 spur_irq_count;
   const u64 ticks = get_ticks();

   if (ticks > TIMER_HZ)
      dp_printk("   Spurious IRQ count: %u (%u/sec)\n",
                spur_irq_count,
                spur_irq_count / (ticks / TIMER_HZ));
   else
      dp_printk("   Spurious IRQ count: %u (< 1 sec)\n",
                spur_irq_count, spur_irq_count);
}

static void debug_dump_unhandled_irq_count(void)
{
   extern u32 unhandled_irq_count[256];
   u32 tot_count = 0;

   for (u32 i = 0; i < ARRAY_SIZE(unhandled_irq_count); i++)
      tot_count += unhandled_irq_count[i];

   if (!tot_count)
      return;

   dp_printk("\n");
   dp_printk("Unhandled IRQs count table\n\n");

   for (u32 i = 0; i < ARRAY_SIZE(unhandled_irq_count); i++) {

      if (unhandled_irq_count[i])
         dp_printk("   IRQ #%3u: %3u unhandled\n", i,
                   unhandled_irq_count[i]);
   }

   dp_printk("\n");
}

void dp_show_irq_stats(void)
{
   dp_printk("\n\n");
   dp_printk("Kernel IRQ-related counters\n\n");

   debug_dump_slow_irq_handler_count();
   debug_dump_spur_irq_count();
   debug_dump_unhandled_irq_count();
}
