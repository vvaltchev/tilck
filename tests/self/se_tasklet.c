/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/tasklet.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/self_tests.h>

static u32 counter;
static u64 cycles_begin;

static void test_tasklet_func()
{
   counter++;
}

static void end_test(void *arg1, void *arg2)
{
   struct kcond *c = arg1;
   struct kmutex *m = arg2;

   const u32 max_tasklets = get_tasklet_runner_limit(0);
   const u32 tot_iters = max_tasklets * 10;

   u64 elapsed = RDTSC() - cycles_begin;
   VERIFY(counter == tot_iters);
   printk("[se_tasklet] Avg cycles per tasklet "
          "(enqueue + execute): %llu\n", elapsed/counter);

   kmutex_lock(m);
   {
      printk("[se_tasklet] end_test() holding the lock and signalling cond\n");
      kcond_signal_one(c);
   }
   kmutex_unlock(m);
   printk("[se_tasklet] end_test() func completed\n");
}

void selftest_tasklet_short(void)
{
   const u32 max_tasklets = get_tasklet_runner_limit(0);
   const u32 tot_iters = max_tasklets * 10;
   struct kcond c;
   struct kmutex m;
   bool added;

   kcond_init(&c);
   kmutex_init(&m, 0);
   counter = 0;

   ASSERT(is_preemption_enabled());
   printk("[se_tasklet] BEGIN\n");

   cycles_begin = RDTSC();

   for (u32 i = 0; i < tot_iters; i++) {

      do {
         added = enqueue_tasklet0(0, &test_tasklet_func);
      } while (!added);
   }

   printk("[se_tasklet] Main test done, now enqueue end_test()\n");
   kmutex_lock(&m);
   {
      printk("[se_tasklet] Under lock, before enqueue\n");

      do {
         added = enqueue_tasklet2(0, &end_test, &c, &m);
      } while (!added);

      printk("[se_tasklet] Under lock, AFTER enqueue\n");
      printk("[se_tasklet] Now, wait on cond\n");
      kcond_wait(&c, &m, KCOND_WAIT_FOREVER);
   }
   kmutex_unlock(&m);
   kcond_destory(&c);
   kmutex_destroy(&m);
   printk("[se_tasklet] END\n");
   regular_self_test_end();
}

void selftest_tasklet_perf_short(void)
{
   bool added;
   u32 n = 0;
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
