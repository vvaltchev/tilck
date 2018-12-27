/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/tasklet.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/self_tests/self_tests.h>

static volatile int counter = 0;
static u32 cycles_begin;

static void test_tasklet_func()
{
   counter++;
}

static void end_test(void *arg)
{
   kcond *c = arg;

   const int max_tasklets = get_tasklet_runner_limit(0);
   const int tot_iters = max_tasklets * 10;

   u64 elapsed = RDTSC() - cycles_begin;
   VERIFY(counter == tot_iters);
   printk("[selftest_tasklet] Avg cycles per tasklet "
          "(enqueue + execute): %llu\n", elapsed/counter);

   kcond_signal_one(c);
}

void selftest_tasklet_short(void)
{
   const int max_tasklets = get_tasklet_runner_limit(0);
   const int tot_iters = max_tasklets * 10;

   bool added;
   counter = 0;

   ASSERT(is_preemption_enabled());
   printk("[selftest_tasklet] BEGIN\n");

   cycles_begin = RDTSC();

   for (int i = 0; i < tot_iters; i++) {

      do {
         added = enqueue_tasklet0(0, &test_tasklet_func);
      } while (!added);

   }

   kcond c;
   kcond_init(&c);

   do {
      added = enqueue_tasklet1(0, &end_test, &c);
   } while (!added);

   kcond_wait(&c, NULL, KCOND_WAIT_FOREVER);
   kcond_destory(&c);
   printk("[selftest_tasklet] END\n");
   regular_self_test_end();
}

void selftest_tasklet_perf_short(void)
{
   bool added;
   int n = 0;

   u64 start, elapsed;

   start = RDTSC();

   while (true) {

      added = enqueue_tasklet0(0, &test_tasklet_func);

      if (!added)
         break;

      n++;
   }

   elapsed = RDTSC() - start;

   ASSERT(n > 0); // SA: avoid division by zero warning
   printk("Avg. tasklet enqueue cycles: %llu [%i tasklets]\n", elapsed/n, n);
   regular_self_test_end();
}
