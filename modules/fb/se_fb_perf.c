/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_debug.h>

#if KERNEL_SELFTESTS

#include <tilck/common/basic_defs.h>
#include <tilck/common/color_defs.h>
#include <tilck/common/printk.h>

#include <tilck/mods/fb_console.h>
#include <tilck/kernel/self_tests.h>
#include <tilck/kernel/hal.h>

#include "fb_int.h"

void internal_selftest_fb_perf(bool use_fpu)
{
   if (!use_framebuffer())
      panic("Unable to test framebuffer's performance: we're in text-mode");

   const int iters = 30;
   u64 start, duration, cycles;

   if (use_fpu)
      fpu_context_begin();
   {
      start = RDTSC();

      for (int i = 0; i < iters; i++) {
         u32 color = vga_rgb_colors[i % 2 ? COLOR_WHITE : COLOR_BLACK];
         fb_raw_perf_screen_redraw(color, use_fpu);
      }

      duration = RDTSC() - start;
   }
   if (use_fpu)
      fpu_context_end();

   cycles = duration / iters;

   u32 pixels = fb_get_width() * fb_get_height();
   printk("fb size (pixels): %u\n", pixels);
   printk("cycles per redraw: %" PRIu64 "\n", cycles);
   printk("cycles per 32 pixels: %" PRIu64 "\n", 32 * cycles / pixels);
   printk("use_fpu: %d\n", use_fpu);

   fb_draw_banner();
}

void selftest_fbperf_nofpu(void)
{
   internal_selftest_fb_perf(false);
}

void selftest_fbperf_fpu(void)
{
   internal_selftest_fb_perf(true);
}

REGISTER_SELF_TEST(fbperf_nofpu, se_manual, &selftest_fbperf_nofpu)
REGISTER_SELF_TEST(fbperf_fpu, se_manual, &selftest_fbperf_fpu)

#endif // #if KERNEL_SELFTESTS
