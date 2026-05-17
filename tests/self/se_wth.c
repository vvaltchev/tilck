/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/atomics.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/worker_thread.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/self_tests.h>

static atomic_u32_t g_counter;
static u64 g_cycles_begin;

struct se_wth_ctx {
   struct worker_thread *wth;
   struct kmutex m;
   struct kcond c;
};

static void test_wth_func(void *arg)
{
   atomic_fetch_add(&g_counter, 1);
}

static void test_wth_func2(void *arg)
{
   atomic_fetch_add(&g_counter, 1);
   kernel_sleep_ms(50);
}

static void end_test(void *arg)
{
   struct se_wth_ctx *ctx = arg;

   const u32 max_jobs = wth_get_queue_size(ctx->wth);
   const u32 tot_iters = max_jobs * 10;

   u64 elapsed = RDTSC() - g_cycles_begin;
   VERIFY(atomic_load(&g_counter) == tot_iters);

   printk("[se_wth] Avg cycles per job "
          "(enqueue + execute): %" PRIu64 "\n",
          elapsed / atomic_load(&g_counter));

   printk("[se_wth] end_test() waiting to grab the lock\n");
   kmutex_lock(&ctx->m);
   {
      printk("[se_wth] end_test() holding the lock and signalling cond\n");
      kcond_signal_one(&ctx->c);
   }
   kmutex_unlock(&ctx->m);
   printk("[se_wth] end_test() func completed\n");
}

void selftest_wth(void)
{
   const u32 attempts_check = 500 * 1000;
   struct se_wth_ctx ctx;
   u32 yields_count = 0;
   u64 tot_attempts = 0;
   u32 last_counter_val;
   u32 counter_now;
   u32 attempts;
   u32 max_jobs;
   u32 tot_iters;
   bool added;

   ctx.wth = wth_find_worker(WTH_PRIO_LOWEST);
   VERIFY(ctx.wth != NULL);

   max_jobs = wth_get_queue_size(ctx.wth);
   tot_iters = max_jobs * 10;

   kcond_init(&ctx.c);
   kmutex_init(&ctx.m, 0);
   atomic_store(&g_counter, 0);

   ASSERT(is_preemption_enabled());
   printk("[se_wth] BEGIN\n");

   g_cycles_begin = RDTSC();

   for (u32 i = 0; i < tot_iters; i++) {

      attempts = 1;
      last_counter_val = atomic_load(&g_counter);
      bool did_yield = false;

      do {

         added = wth_enqueue_on(ctx.wth, &test_wth_func, NULL);
         attempts++;

         if (!(attempts % attempts_check)) {

            counter_now = atomic_load(&g_counter);

            if (counter_now == last_counter_val) {

               if (did_yield)
                  panic("It seems that jobs don't get executed");

               did_yield = true;
               yields_count++;
               kernel_yield();
            }
         }

      } while (!added);

      tot_attempts += attempts;
   }

   last_counter_val = atomic_load(&g_counter);
   printk("[se_wth] Main test done\n");
   printk("[se_wth] AVG attempts: %u\n", (u32)(tot_attempts/tot_iters));
   printk("[se_wth] Yields:       %u\n", yields_count);
   printk("[se_wth] counter now:  %u\n", last_counter_val);
   printk("[se_wth] now wait for completion...\n");

   kernel_sleep(1);
   attempts = 0;

   do {

      counter_now = atomic_load(&g_counter);

      if (counter_now == last_counter_val) {

         /*
          * Note: we must keep in mind that we cannot rely that 100% of the time
          * after kernel_sleep() the worker thread will be able to run, without
          * being preempted and increment our `g_counter`. In theory we might
          * still get there first.
          */

         if (attempts++ >= 3)
            panic("It seems that jobs don't get executed");

         kernel_sleep(TIME_SLICE_TICKS);
         continue;
      }

      attempts = 0;
      last_counter_val = counter_now;
      kernel_sleep(1);

   } while (counter_now < tot_iters);

   printk("[se_wth] DONE, counter: %u\n", counter_now);
   printk("[se_wth] enqueue end_test()\n");
   kmutex_lock(&ctx.m);
   {
      printk("[se_wth] Under lock, before enqueue\n");

      do {
         added = wth_enqueue_on(ctx.wth, &end_test, &ctx);
      } while (!added);

      printk("[se_wth] Under lock, AFTER enqueue\n");
      printk("[se_wth] Now, wait on cond\n");
      kcond_wait(&ctx.c, &ctx.m, KCOND_WAIT_FOREVER);
   }
   kmutex_unlock(&ctx.m);
   kcond_destroy(&ctx.c);
   kmutex_destroy(&ctx.m);
   printk("[se_wth] END\n");
   se_regular_end();
}

REGISTER_SELF_TEST(wth, se_short, &selftest_wth)

void selftest_wth2(void)
{
   struct worker_thread *wth;
   bool added;
   int cnt = 0;

   atomic_store(&g_counter, 0);
   wth = wth_find_worker(WTH_PRIO_LOWEST);
   ASSERT(wth != NULL);

   printk("[se_wth] enqueue 10 jobs\n");

   while (cnt < 10) {

      added = wth_enqueue_on(wth, &test_wth_func2, NULL);

      if (added)
         cnt++;
   }

   printk("[se_wth] done\n");
   printk("[se_wth] wait for completion\n");
   wth_wait_for_completion(wth);

   if (atomic_load(&g_counter) != 10)
      panic("[se_wth] counter (%d) != 10", atomic_load(&g_counter));

   printk("[se_wth] everything is OK\n");
}

REGISTER_SELF_TEST(wth2, se_short, &selftest_wth2)

void selftest_wth_perf(void)
{
   struct worker_thread *wth = wth_find_worker(WTH_PRIO_LOWEST);
   bool added;
   u32 n = 0;
   u64 start, elapsed;

   /*
    * Suppress the producer/worker race for the duration of the
    * measurement by holding preemption disabled across the loop. The
    * first wth_enqueue_on() triggers wth_wakeup(), which sets
    * need_resched; with preemption disabled here, the matching
    * enable_preemption() inside wth_enqueue_on() can't yield to the
    * worker (we're nested), so the queue actually fills to its
    * capacity and the loop terminates deterministically on the first
    * `!added`. Without this scaffolding, the test relied on the
    * producer happening to out-pace the worker — a timing-dependent
    * assumption that doesn't hold when the worker drains as fast as
    * the producer enqueues (the queue stays at 0–1 items forever).
    *
    * What we end up measuring is the raw cost of a successful
    * wth_enqueue_on() call when the queue has room — which is what
    * "enqueue cycles" should mean here. enable_preemption() at the
    * end honors the pending need_resched and finally yields to the
    * worker to drain.
    */
   disable_preemption();
   {
      start = RDTSC();

      do {

         added = wth_enqueue_on(wth, &test_wth_func, NULL);

         if (added)
            n++;

      } while (added);

      elapsed = RDTSC() - start;
   }
   enable_preemption();

   ASSERT(n > 0); // SA: avoid division by zero warning
   printk("Avg. job enqueue cycles: %" PRIu64 " [%i jobs]\n", elapsed/n, n);
   se_regular_end();
}

REGISTER_SELF_TEST(wth_perf, se_short, &selftest_wth_perf)
