/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * IRQs panel. Renders the same counters the in-kernel
 * modules/debugpanel/dp_irqs.c showed (slow-timer counter, spurious
 * IRQ count + rate, unhandled IRQ table, unmasked legacy IRQ list),
 * but driven by a TILCK_CMD_DP_GET_IRQ_STATS snapshot instead of
 * direct kernel-symbol access.
 */

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>

#include <tilck/common/syscalls.h>
#include <tilck/common/dp_abi.h>

#include "termutil.h"
#include "dp_int.h"

static struct dp_irq_stats stats;
static int got_stats;
static int row;

/*
 * The kernel's KRN_TIMER_HZ is exposed under /syst/kernel/timer_hz.
 * Read it once at first_setup time so the spurious-IRQ rate uses the
 * right divisor.
 */
static unsigned long timer_hz;

static long dp_cmd_get_irqs(struct dp_irq_stats *out)
{
   return syscall(TILCK_CMD_SYSCALL,
                  TILCK_CMD_DP_GET_IRQ_STATS,
                  (long)out, 0L, 0L, 0L);
}

static unsigned long read_ulong_from(const char *path, unsigned long fallback)
{
   int fd = open(path, 0 /* O_RDONLY */);
   if (fd < 0)
      return fallback;

   char buf[32] = {0};
   ssize_t n = read(fd, buf, sizeof(buf) - 1);
   close(fd);

   if (n <= 0)
      return fallback;

   buf[n] = 0;

   unsigned long v = 0;
   for (int i = 0; buf[i] >= '0' && buf[i] <= '9'; i++)
      v = v * 10 + (unsigned long)(buf[i] - '0');

   return v ? v : fallback;
}

static void dp_irqs_first_setup(void)
{
   timer_hz = read_ulong_from("/syst/kernel/timer_hz", 250);
}

static void dp_irqs_on_enter(void)
{
   got_stats = (dp_cmd_get_irqs(&stats) == 0);
}

static void dp_show_irqs(void)
{
   row = dp_screen_start_row;

   dp_writeln("Kernel IRQ-related counters");

   if (!got_stats) {
      dp_writeln(E_COLOR_BR_RED
                 "TILCK_CMD_DP_GET_IRQ_STATS failed (module not loaded?)"
                 RESET_ATTRS);
      return;
   }

   if (stats.slow_timer_count) {
      dp_writeln("   Slow timer irq handler counter: %u",
                 stats.slow_timer_count);
   }

   if (stats.ticks_at_snapshot > timer_hz) {
      const unsigned long secs =
         (unsigned long)stats.ticks_at_snapshot / timer_hz;
      dp_writeln("   Spurious IRQ count: %u (%lu/sec)",
                 stats.spur_irq_count,
                 secs ? stats.spur_irq_count / secs : 0UL);
   } else {
      dp_writeln("   Spurious IRQ count: %u", stats.spur_irq_count);
   }

   /* Unhandled IRQ table */
   unsigned int tot_unhandled = 0;
   for (int i = 0; i < (int)(sizeof(stats.unhandled_count) /
                             sizeof(stats.unhandled_count[0])); i++)
   {
      tot_unhandled += stats.unhandled_count[i];
   }

   if (tot_unhandled) {

      dp_writeln(" ");
      dp_writeln("Unhandled IRQs count table");

      for (int i = 0; i < (int)(sizeof(stats.unhandled_count) /
                                sizeof(stats.unhandled_count[0])); i++)
      {
         if (stats.unhandled_count[i])
            dp_writeln("   IRQ #%3d: %3u unhandled",
                       i, stats.unhandled_count[i]);
      }
   }

   /* Unmasked legacy IRQs (0..15) */
   dp_writeln(" ");
   dp_writeln("Unmasked IRQs:");
   row--;          /* keep the next prints on the same line as the header */
   dp_write_raw(" ");

   for (int i = 0; i < 16; i++) {
      if (stats.unmasked_mask_lo16 & (1u << i))
         dp_write_raw("#%d ", i);
   }

   dp_writeln(" ");
}

static struct dp_screen dp_irqs_screen = {
   .index = 4,
   .label = "IRQs",
   .draw_func = dp_show_irqs,
   .first_setup = dp_irqs_first_setup,
   .on_dp_enter = dp_irqs_on_enter,
};

__attribute__((constructor))
static void dp_irqs_register(void)
{
   dp_register_screen(&dp_irqs_screen);
}
