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
#include "fake_funcs_utils.h"
#include "kernel_init_funcs.h"

extern "C" {

   #include <tilck/kernel/kmalloc.h>
   #include <tilck/kernel/worker_thread.h>
   #include "kernel/wth_int.h" // private header
}

using namespace std;
using namespace testing;

static void destroy_last_worker_thread(void)
{
   assert(worker_threads_cnt > 0);

   const u32 wth = --worker_threads_cnt;
   struct worker_thread *t = worker_threads[wth];
   const u32 queue_size = t->rb.max_elems;
   assert(t != NULL);

   safe_ringbuf_destory(&t->rb);
   kfree_array_obj(t->jobs, struct wjob, queue_size);
   kfree_obj(t, struct worker_thread);
   bzero((void *)t, sizeof(*t));
   worker_threads[wth] = NULL;
}

class worker_thread_test : public Test {

   void SetUp() override {
      init_kmalloc_for_tests();
      init_worker_threads();
   }

   void TearDown() override {
      destroy_last_worker_thread();
   }
};

void simple_func1(void *p1)
{
   ASSERT_EQ(p1, TO_PTR(1234));
}

TEST_F(worker_thread_test, essential)
{
   bool res = false;
   struct worker_thread *wth = wth_find_worker(WTH_PRIO_HIGHEST);

   ASSERT_TRUE(wth_enqueue_on(wth, &simple_func1, TO_PTR(1234)));
   ASSERT_NO_FATAL_FAILURE({ res = wth_process_single_job(wth); });
   ASSERT_TRUE(res);
}


TEST_F(worker_thread_test, base)
{
   struct worker_thread *wth = wth_find_worker(WTH_PRIO_HIGHEST);
   const int max_jobs = wth_get_queue_size(wth);
   bool res;

   for (int i = 0; i < max_jobs; i++) {
      res = wth_enqueue_on(wth, &simple_func1, TO_PTR(1234));
      ASSERT_TRUE(res);
   }

   res = wth_enqueue_on(wth, &simple_func1, TO_PTR(1234));

   // There is no more space left, expecting the ADD failed.
   ASSERT_FALSE(res);

   for (int i = 0; i < max_jobs; i++) {
      ASSERT_NO_FATAL_FAILURE({ res = wth_process_single_job(wth); });
      ASSERT_TRUE(res);
   }

   ASSERT_NO_FATAL_FAILURE({ res = wth_process_single_job(wth); });

   // There are no more jobs, expecting the RUN failed.
   ASSERT_FALSE(res);
}


TEST_F(worker_thread_test, advanced)
{
   struct worker_thread *wth = wth_find_worker(WTH_PRIO_HIGHEST);
   const int max_jobs = wth_get_queue_size(wth);
   bool res;

   // Fill half of the buffer.
   for (int i = 0; i < max_jobs/2; i++) {
      res = wth_enqueue_on(wth, &simple_func1, TO_PTR(1234));
      ASSERT_TRUE(res);
   }

   // Consume 1/4.
   for (int i = 0; i < max_jobs/4; i++) {
      ASSERT_NO_FATAL_FAILURE({ res = wth_process_single_job(wth); });
      ASSERT_TRUE(res);
   }

   // Fill half of the buffer.
   for (int i = 0; i < max_jobs/2; i++) {
      res = wth_enqueue_on(wth, &simple_func1, TO_PTR(1234));
      ASSERT_TRUE(res);
   }

   // Consume 2/4
   for (int i = 0; i < max_jobs/2; i++) {
      ASSERT_NO_FATAL_FAILURE({ res = wth_process_single_job(wth); });
      ASSERT_TRUE(res);
   }

   // Fill half of the buffer.
   for (int i = 0; i < max_jobs/2; i++) {
      res = wth_enqueue_on(wth, &simple_func1, TO_PTR(1234));
      ASSERT_TRUE(res);
   }

   // Now the cyclic buffer for sure rotated.

   // Consume 3/4
   for (int i = 0; i < 3*max_jobs/4; i++) {
      ASSERT_NO_FATAL_FAILURE({ res = wth_process_single_job(wth); });
      ASSERT_TRUE(res);
   }

   ASSERT_NO_FATAL_FAILURE({ res = wth_process_single_job(wth); });

   // There are no more jobs, expecting the RUN failed.
   ASSERT_FALSE(res);
}

TEST_F(worker_thread_test, chaos)
{
   struct worker_thread *wth = wth_find_worker(WTH_PRIO_HIGHEST);
   const int max_jobs = wth_get_queue_size(wth);

   random_device rdev;
   default_random_engine e(rdev());

   lognormal_distribution<> dist(3.0, 2.5);

   int slots_used = 0;
   bool res = false;

   for (int iters = 0; iters < 10000; iters++) {

      int c;
      c = round(dist(e));

      for (int i = 0; i < c; i++) {

         if (slots_used == max_jobs) {
            ASSERT_FALSE(wth_enqueue_on(wth, &simple_func1, TO_PTR(1234)));
            break;
         }

         res = wth_enqueue_on(wth, &simple_func1, TO_PTR(1234));
         ASSERT_TRUE(res);
         slots_used++;
      }

      c = round(dist(e));

      for (int i = 0; i < c; i++) {

         if (slots_used == 0) {
            ASSERT_NO_FATAL_FAILURE({ res = wth_process_single_job(wth); });
            ASSERT_FALSE(res);
            break;
         }

         ASSERT_NO_FATAL_FAILURE({ res = wth_process_single_job(wth); });
         ASSERT_TRUE(res);
         slots_used--;
      }
   }
}
