/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cassert>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <random>

#include <gtest/gtest.h>
#include "mocks.h"
#include "kernel_init_funcs.h"

extern "C" {

   #include <tilck/kernel/kmalloc.h>
   #include <tilck/kernel/tasklet.h>
   #include "kernel/tasklet_int.h" // private header

   extern u32 worker_threads_cnt;

   void destroy_last_tasklet_thread(void)
   {
      assert(worker_threads_cnt > 0);

      const u32 wth = --worker_threads_cnt;
      struct worker_thread *t = worker_threads[wth];
      assert(t != NULL);

      safe_ringbuf_destory(&t->rb);
      kfree2(t->jobs, sizeof(struct wjob) * t->limit);
      kfree2(t, sizeof(struct worker_thread));
      bzero((void *)t, sizeof(*t));
      worker_threads[wth] = NULL;
   }
}

using namespace std;
using namespace testing;

class tasklet_test : public Test {

   void SetUp() override {
      init_kmalloc_for_tests();
      init_worker_threads();
   }

   void TearDown() override {
      destroy_last_tasklet_thread();
   }
};

void simple_func1(void *p1)
{
   ASSERT_EQ(p1, TO_PTR(1234));
}

TEST_F(tasklet_test, essential)
{
   bool res = false;

   ASSERT_TRUE(enqueue_job(0, &simple_func1, TO_PTR(1234)));
   ASSERT_NO_FATAL_FAILURE({ res = wth_process_single_job(0); });
   ASSERT_TRUE(res);
}


TEST_F(tasklet_test, base)
{
   const int max_tasklets = get_worker_queue_size(0);
   bool res;

   for (int i = 0; i < max_tasklets; i++) {
      res = enqueue_job(0, &simple_func1, TO_PTR(1234));
      ASSERT_TRUE(res);
   }

   res = enqueue_job(0, &simple_func1, TO_PTR(1234));

   // There is no more space left, expecting the ADD failed.
   ASSERT_FALSE(res);

   for (int i = 0; i < max_tasklets; i++) {
      ASSERT_NO_FATAL_FAILURE({ res = wth_process_single_job(0); });
      ASSERT_TRUE(res);
   }

   ASSERT_NO_FATAL_FAILURE({ res = wth_process_single_job(0); });

   // There are no more jobs, expecting the RUN failed.
   ASSERT_FALSE(res);
}


TEST_F(tasklet_test, advanced)
{
   const int max_tasklets = get_worker_queue_size(0);
   bool res;

   // Fill half of the buffer.
   for (int i = 0; i < max_tasklets/2; i++) {
      res = enqueue_job(0, &simple_func1, TO_PTR(1234));
      ASSERT_TRUE(res);
   }

   // Consume 1/4.
   for (int i = 0; i < max_tasklets/4; i++) {
      ASSERT_NO_FATAL_FAILURE({ res = wth_process_single_job(0); });
      ASSERT_TRUE(res);
   }

   // Fill half of the buffer.
   for (int i = 0; i < max_tasklets/2; i++) {
      res = enqueue_job(0, &simple_func1, TO_PTR(1234));
      ASSERT_TRUE(res);
   }

   // Consume 2/4
   for (int i = 0; i < max_tasklets/2; i++) {
      ASSERT_NO_FATAL_FAILURE({ res = wth_process_single_job(0); });
      ASSERT_TRUE(res);
   }

   // Fill half of the buffer.
   for (int i = 0; i < max_tasklets/2; i++) {
      res = enqueue_job(0, &simple_func1, TO_PTR(1234));
      ASSERT_TRUE(res);
   }

   // Now the cyclic buffer for sure rotated.

   // Consume 3/4
   for (int i = 0; i < 3*max_tasklets/4; i++) {
      ASSERT_NO_FATAL_FAILURE({ res = wth_process_single_job(0); });
      ASSERT_TRUE(res);
   }

   ASSERT_NO_FATAL_FAILURE({ res = wth_process_single_job(0); });

   // There are no more jobs, expecting the RUN failed.
   ASSERT_FALSE(res);
}

TEST_F(tasklet_test, chaos)
{
   const int max_tasklets = get_worker_queue_size(0);

   random_device rdev;
   default_random_engine e(rdev());

   lognormal_distribution<> dist(3.0, 2.5);

   int slots_used = 0;
   bool res = false;

   for (int iters = 0; iters < 10000; iters++) {

      int c;
      c = round(dist(e));

      for (int i = 0; i < c; i++) {

         if (slots_used == max_tasklets) {
            ASSERT_FALSE(enqueue_job(0, &simple_func1, TO_PTR(1234)));
            break;
         }

         res = enqueue_job(0, &simple_func1, TO_PTR(1234));
         ASSERT_TRUE(res);
         slots_used++;
      }

      c = round(dist(e));

      for (int i = 0; i < c; i++) {

         if (slots_used == 0) {
            ASSERT_NO_FATAL_FAILURE({ res = wth_process_single_job(0); });
            ASSERT_FALSE(res);
            break;
         }

         ASSERT_NO_FATAL_FAILURE({ res = wth_process_single_job(0); });
         ASSERT_TRUE(res);
         slots_used--;
      }
   }
}
