/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/rwlock.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/self_tests.h>
#include <tilck/kernel/timer.h>

#define RWLOCK_TH_ITERS    1000
#define RWLOCK_READERS       20
#define RWLOCK_WRITERS       20
#define RETRY_COUNT           4

static struct rwlock_rp test_rwlrp;
static struct rwlock_wp test_rwlwp;

static int se_rwlock_vars[3];
static const int se_rwlock_set_1[3] = {1, 2, 3};
static const int se_rwlock_set_2[3] = {10, 20, 30};
static atomic_int_t readers_running;
static atomic_int_t writers_running;

/*
 * Diagnostic instrumentation: per-thread start/end-tick records +
 * per-thread completed-iteration counters. Slot assigned by
 * atomic_fetch_add on enter. Lets us see, on a failed retry,
 * whether readers finished long before writers (the CFS scheduler
 * favouring readers-first hypothesis) or whether the two groups
 * interleaved normally and the failure window was a fluke. The
 * iteration counters distinguish "writers got blocked at the
 * very first exlock()" (counter == 0) from "writers made some
 * progress but not enough" (counter > 0).
 *
 * Cheap: one atomic increment per thread + two timestamps + one
 * counter increment per iter; no extra printk inside the work loop.
 */
static atomic_int_t reader_slot_idx;
static atomic_int_t writer_slot_idx;
static u64 reader_start_ticks[RWLOCK_READERS];
static u64 reader_end_ticks[RWLOCK_READERS];
static u64 writer_start_ticks[RWLOCK_WRITERS];
static u64 writer_end_ticks[RWLOCK_WRITERS];
static int reader_iters[RWLOCK_READERS];
static int writer_iters[RWLOCK_WRITERS];

static void diag_reset(void)
{
   atomic_store(&reader_slot_idx, 0);
   atomic_store(&writer_slot_idx, 0);
   for (int i = 0; i < RWLOCK_READERS; i++) {
      reader_start_ticks[i] = 0;
      reader_end_ticks[i] = 0;
      reader_iters[i] = 0;
   }
   for (int i = 0; i < RWLOCK_WRITERS; i++) {
      writer_start_ticks[i] = 0;
      writer_end_ticks[i] = 0;
      writer_iters[i] = 0;
   }
}

static void dump_diag(int sub_test, int retry, u64 sub_test_start_tick)
{
   printk(NO_PREFIX "[diag s%d/%d] sub-test start tick: %llu\n",
          sub_test, retry,
          (unsigned long long) sub_test_start_tick);

   printk(NO_PREFIX "[diag s%d/%d] reader starts:", sub_test, retry);
   for (int i = 0; i < RWLOCK_READERS; i++)
      printk(NO_PREFIX " %llu", (unsigned long long) reader_start_ticks[i]);

   printk(NO_PREFIX "\n[diag s%d/%d] reader ends:  ", sub_test, retry);
   for (int i = 0; i < RWLOCK_READERS; i++)
      printk(NO_PREFIX " %llu", (unsigned long long) reader_end_ticks[i]);

   printk(NO_PREFIX "\n[diag s%d/%d] reader iters: ", sub_test, retry);
   for (int i = 0; i < RWLOCK_READERS; i++)
      printk(NO_PREFIX " %d", reader_iters[i]);

   printk(NO_PREFIX "\n[diag s%d/%d] writer starts:", sub_test, retry);
   for (int i = 0; i < RWLOCK_WRITERS; i++)
      printk(NO_PREFIX " %llu", (unsigned long long) writer_start_ticks[i]);

   printk(NO_PREFIX "\n[diag s%d/%d] writer ends:  ", sub_test, retry);
   for (int i = 0; i < RWLOCK_WRITERS; i++)
      printk(NO_PREFIX " %llu", (unsigned long long) writer_end_ticks[i]);

   printk(NO_PREFIX "\n[diag s%d/%d] writer iters: ", sub_test, retry);
   for (int i = 0; i < RWLOCK_WRITERS; i++)
      printk(NO_PREFIX " %d", writer_iters[i]);

   printk(NO_PREFIX "\n");
}

struct se_rwlock_ctx {

   void (*shlock)(void *);
   void (*shunlock)(void *);
   void (*exlock)(void *);
   void (*exunlock)(void *);
   void *arg;
};

static struct se_rwlock_ctx se_rp_ctx =
{
   .shlock = (void *) rwlock_rp_shlock,
   .shunlock = (void *) rwlock_rp_shunlock,
   .exlock = (void *) rwlock_rp_exlock,
   .exunlock = (void *) rwlock_rp_exunlock,
   .arg = (void *) &test_rwlrp,
};

static struct se_rwlock_ctx se_wp_ctx =
{
   .shlock = (void *) rwlock_wp_shlock,
   .shunlock = (void *) rwlock_wp_shunlock,
   .exlock = (void *) rwlock_wp_exlock,
   .exunlock = (void *) rwlock_wp_exunlock,
   .arg = (void *) &test_rwlwp,
};


static void se_rwlock_set_vars(const int *set)
{
   for (int i = 0; i < ARRAY_SIZE(se_rwlock_vars); i++) {
      se_rwlock_vars[i] = set[i];
      kernel_yield();
   }
}

static void se_rwlock_check_set_eq(const int *set)
{
   for (int i = 0; i < ARRAY_SIZE(se_rwlock_vars); i++) {
      VERIFY(se_rwlock_vars[i] == set[i]);
      kernel_yield();
   }
}

static void se_rwlock_read_thread(void *arg)
{
   struct se_rwlock_ctx *ctx = arg;
   const int slot = atomic_fetch_add(&reader_slot_idx, 1);

   if (slot < RWLOCK_READERS)
      reader_start_ticks[slot] = get_ticks();

   atomic_fetch_add(&readers_running, 1);

   for (int iter = 0; iter < RWLOCK_TH_ITERS; iter++) {

      if (se_is_stop_requested())
         break;

      ctx->shlock(ctx->arg);
      {
         if (se_rwlock_vars[0] == se_rwlock_set_1[0])
            se_rwlock_check_set_eq(se_rwlock_set_1);
         else
            se_rwlock_check_set_eq(se_rwlock_set_2);
      }
      ctx->shunlock(ctx->arg);

      if (slot < RWLOCK_READERS)
         reader_iters[slot] = iter + 1;
   }

   atomic_fetch_sub(&readers_running, 1);

   if (slot < RWLOCK_READERS)
      reader_end_ticks[slot] = get_ticks();
}

static void se_rwlock_write_thread(void *arg)
{
   struct se_rwlock_ctx *ctx = arg;
   const int slot = atomic_fetch_add(&writer_slot_idx, 1);

   if (slot < RWLOCK_WRITERS)
      writer_start_ticks[slot] = get_ticks();

   atomic_fetch_add(&writers_running, 1);

   for (int iter = 0; iter < RWLOCK_TH_ITERS; iter++) {

      if (se_is_stop_requested())
         break;

      ctx->exlock(ctx->arg);
      {
         kernel_yield();

         if (se_rwlock_vars[0] == se_rwlock_set_1[0]) {

            se_rwlock_check_set_eq(se_rwlock_set_1);
            se_rwlock_set_vars(se_rwlock_set_2);

         } else {

            se_rwlock_check_set_eq(se_rwlock_set_2);
            se_rwlock_set_vars(se_rwlock_set_1);
         }

         kernel_yield();
      }
      ctx->exunlock(ctx->arg);

      if (slot < RWLOCK_WRITERS)
         writer_iters[slot] = iter + 1;
   }

   atomic_fetch_sub(&writers_running, 1);

   if (slot < RWLOCK_WRITERS)
      writer_end_ticks[slot] = get_ticks();
}

static void se_rwlock_common(int *rt, int *wt, struct se_rwlock_ctx *ctx)
{
   se_rwlock_set_vars(se_rwlock_set_1);

   for (u32 i = 0; i < RWLOCK_READERS; i++) {
      rt[i] = kthread_create(se_rwlock_read_thread, 0, ctx);
      VERIFY(rt[i] > 0);
   }

   for (u32 i = 0; i < RWLOCK_WRITERS; i++) {
      wt[i] = kthread_create(se_rwlock_write_thread, 0, ctx);
      VERIFY(wt[i] > 0);
   }
}

void selftest_rwlock_rp(void)
{
   int rt[RWLOCK_READERS];
   int wt[RWLOCK_WRITERS];
   int retry;

   atomic_store(&readers_running, 0);
   atomic_store(&writers_running, 0);
   rwlock_rp_init(&test_rwlrp);

   /*
    * Since we're testing a read-preferring rwlock, we except that, after all
    * readers have finished, there will still be writers running. At the same
    * way, we expect that, if we join first the writers, there won't be any
    * running readers.
    */

   printk("-------- sub-test: join readers and then writers -----------\n");
   for (retry = 0; retry < RETRY_COUNT; retry++) {

      se_rwlock_common(rt, wt, &se_rp_ctx);
      kthread_join_all(rt, ARRAY_SIZE(rt), true);
      printk("After readers, running writers: %d\n",
             atomic_load(&writers_running));

      if (atomic_load(&writers_running) == 0) {

         kthread_join_all(wt, ARRAY_SIZE(wt), true);

         if (se_is_stop_requested())
            break;

         printk("running writers == 0, expected > 0. Re-try sub-test\n");
         continue;
      }

      /* writers_running > 0: ideal case */
      kthread_join_all(wt, ARRAY_SIZE(wt), true);
      break;
   }

   VERIFY(retry < RETRY_COUNT);

   if (se_is_stop_requested())
      goto end;

   printk("-------- sub-test: join writers and then readers -----------\n");
   for (retry = 0; retry < RETRY_COUNT; retry++) {

      se_rwlock_common(rt, wt, &se_rp_ctx);
      kthread_join_all(wt, ARRAY_SIZE(wt), true);

      if (se_is_stop_requested())
         goto end;

      printk("After writers, running readers: %d\n",
             atomic_load(&readers_running));

      if (atomic_load(&readers_running) > 0) {

         kthread_join_all(rt, ARRAY_SIZE(rt), true);

         if (se_is_stop_requested())
            break;

         printk("running readers > 0, expected == 0. Re-try subtest.\n");
         continue;
      }

      /* readers_running == 0: ideal case */
      kthread_join_all(rt, ARRAY_SIZE(rt), true);
      break;
   }
   VERIFY(retry < RETRY_COUNT);

end:
   rwlock_rp_destroy(&test_rwlrp);

   if (se_is_stop_requested())
      se_interrupted_end();
   else
      se_regular_end();
}

REGISTER_SELF_TEST(rwlock_rp, se_med, &selftest_rwlock_rp)

void selftest_rwlock_wp(void)
{
   int rt[RWLOCK_READERS];
   int wt[RWLOCK_WRITERS];
   int retry;

   atomic_store(&readers_running, 0);
   atomic_store(&writers_running, 0);
   rwlock_wp_init(&test_rwlwp, false);

   printk("-------- sub-test: join readers and then writers -----------\n");

   /*
    * Same as above, but in this case we're testing a write-preferring rwlock.
    * Therefore, after joining the readers, there should be 0 writers running;
    * after joining the writers, there should be some readers running.
    */

   for (retry = 0; retry < RETRY_COUNT; retry++) {

      u64 sub_test_start_tick;

      diag_reset();
      sub_test_start_tick = get_ticks();

      se_rwlock_common(rt, wt, &se_wp_ctx);
      kthread_join_all(rt, ARRAY_SIZE(rt), true);
      printk("After readers, running writers: %d\n",
             atomic_load(&writers_running));

      if (atomic_load(&writers_running) > 0) {

         kthread_join_all(wt, ARRAY_SIZE(wt), true);

         dump_diag(1, retry, sub_test_start_tick);

         if (se_is_stop_requested())
            break;

         printk("running writers > 0, expected == 0. Re-try sub-test.\n");
         continue;
      }

      /* writers_running == 0: that's exactly we'd expect in the ideal case */
      kthread_join_all(wt, ARRAY_SIZE(wt), true);
      break;
   }
   VERIFY(retry < RETRY_COUNT);

   if (se_is_stop_requested())
      goto end;

   printk("-------- sub-test: join writers and then readers -----------\n");

   for (retry = 0; retry < RETRY_COUNT; retry++) {

      u64 sub_test_start_tick;

      diag_reset();
      sub_test_start_tick = get_ticks();

      se_rwlock_common(rt, wt, &se_wp_ctx);
      kthread_join_all(wt, ARRAY_SIZE(wt), true);
      printk("After writers, running readers: %d\n",
             atomic_load(&readers_running));

      if (atomic_load(&readers_running) == 0) {

         kthread_join_all(rt, ARRAY_SIZE(rt), true);

         dump_diag(2, retry, sub_test_start_tick);

         if (se_is_stop_requested())
            break;

         printk("running readers == 0, expected > 0. Re-try sub-test\n");
         continue;
      }

      /* readers_running > 0: ideal case */
      kthread_join_all(rt, ARRAY_SIZE(rt), true);
      break;
   }
   VERIFY(retry < RETRY_COUNT);

end:
   rwlock_wp_destroy(&test_rwlwp);

   if (se_is_stop_requested())
      se_interrupted_end();
   else
      se_regular_end();
}

REGISTER_SELF_TEST(rwlock_wp, se_med, &selftest_rwlock_wp)
