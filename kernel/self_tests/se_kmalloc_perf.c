/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/hal.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/debug_utils.h>

#include "se_data.h"

static void **allocations;

static void kmalloc_perf_print_iters(int iters)
{
   printk("[%2d%s iters] ",
          iters < 1000
            ? iters
            : (iters < 1000000 ? iters / 1000 : iters / 1000000),
          iters < 1000
            ? ""
            : (iters < 1000000 ? "K" : "M"));
}

static void kmalloc_perf_per_size(int size)
{
   const int iters = size < 4096 ? 10000 : (size <= 16*KB ? 1000 : 100);
   u64 start, duration;

   start = RDTSC();

   for (int i = 0; i < iters; i++) {

      allocations[i] = kmalloc(size);

      if (!allocations[i])
         panic("We were unable to allocate %u bytes\n", size);
   }

   for (int i = 0; i < iters; i++) {
      kfree2(allocations[i], size);
   }

   duration = RDTSC() - start;
   kmalloc_perf_print_iters(iters);
   printk(NO_PREFIX "Cycles per kmalloc(%6i) + kfree: %llu\n",
          size, duration  / iters);
}

void selftest_kmalloc_perf_med(void)
{
   const int iters = 1000;
   printk("*** kmalloc perf test ***\n", iters);

   allocations = kmalloc(10000 * sizeof(void *));

   if (!allocations)
      panic("No enough memory for the 'allocations' buffer");

   u64 start = RDTSC();

   for (int i = 0; i < iters; i++) {

      for (int j = 0; j < RANDOM_VALUES_COUNT; j++) {

         allocations[j] = kmalloc(random_values[j]);

         if (!allocations[j])
            panic("We were unable to allocate %u bytes\n", random_values[j]);
      }

      for (int j = 0; j < RANDOM_VALUES_COUNT; j++)
         kfree2(allocations[j], random_values[j]);
   }

   u64 duration = (RDTSC() - start) / (iters * RANDOM_VALUES_COUNT);

   kmalloc_perf_print_iters(iters * RANDOM_VALUES_COUNT);
   printk(NO_PREFIX "Cycles per kmalloc(RANDOM) + kfree: %llu\n", duration);

   for (int s = 32; s <= 256*KB; s *= 2) {
      kmalloc_perf_per_size(s);
   }

   kfree2(allocations, 10000 * sizeof(void *));
   debug_qemu_turn_off_machine();
}
