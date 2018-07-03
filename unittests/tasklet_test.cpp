
#include <common/basic_defs.h>

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
   #include <exos/tasklet.h>
   #include "kernel/tasklet_int.h" // private header
}

using namespace std;
using namespace testing;

class tasklet_test : public Test {

   void SetUp() override {

      if (tasklet_threads[0] != NULL)
         destroy_last_tasklet_thread();

      init_kmalloc_for_tests();
      init_tasklets();
   }

   void TearDown() override {
      /* do nothing, for the moment */
   }
};

void simple_func2(void *p1, void *p2)
{
   ASSERT_EQ(p1, (void*) 1);
   ASSERT_EQ(p2, (void*) 2);
}

void simple_func1(void *p1)
{
   ASSERT_EQ(p1, (void*) 1);
}

TEST_F(tasklet_test, essential)
{
   bool res = false;

   ASSERT_TRUE(enqueue_tasklet2(0, &simple_func2, 1, 2));
   ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(0); });
   ASSERT_TRUE(res);

   ASSERT_TRUE(enqueue_tasklet1(0, &simple_func1, 1));
   ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(0); });
   ASSERT_TRUE(res);
}


TEST_F(tasklet_test, base)
{
   const int max_tasklets = get_tasklet_runner_limit(0);
   bool res;

   for (int i = 0; i < max_tasklets; i++) {
      res = enqueue_tasklet2(0, &simple_func2, 1, 2);
      ASSERT_TRUE(res);
   }

   res = enqueue_tasklet2(0, &simple_func2, 1, 2);

   // There is no more space left, expecting the ADD failed.
   ASSERT_FALSE(res);

   for (int i = 0; i < max_tasklets; i++) {
      ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(0); });
      ASSERT_TRUE(res);
   }

   ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(0); });

   // There are no more tasklets, expecting the RUN failed.
   ASSERT_FALSE(res);
}


TEST_F(tasklet_test, advanced)
{
   const int max_tasklets = get_tasklet_runner_limit(0);
   bool res;

   // Fill half of the buffer.
   for (int i = 0; i < max_tasklets/2; i++) {
      res = enqueue_tasklet2(0, &simple_func2, 1, 2);
      ASSERT_TRUE(res);
   }

   // Consume 1/4.
   for (int i = 0; i < max_tasklets/4; i++) {
      ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(0); });
      ASSERT_TRUE(res);
   }

   // Fill half of the buffer.
   for (int i = 0; i < max_tasklets/2; i++) {
      res = enqueue_tasklet2(0, &simple_func2, 1, 2);
      ASSERT_TRUE(res);
   }

   // Consume 2/4
   for (int i = 0; i < max_tasklets/2; i++) {
      ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(0); });
      ASSERT_TRUE(res);
   }

   // Fill half of the buffer.
   for (int i = 0; i < max_tasklets/2; i++) {
      res = enqueue_tasklet2(0, &simple_func2, 1, 2);
      ASSERT_TRUE(res);
   }

   // Now the cyclic buffer for sure rotated.

   // Consume 3/4
   for (int i = 0; i < 3*max_tasklets/4; i++) {
      ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(0); });
      ASSERT_TRUE(res);
   }

   ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(0); });

   // There are no more tasklets, expecting the RUN failed.
   ASSERT_FALSE(res);
}

TEST_F(tasklet_test, chaos)
{
   const int max_tasklets = get_tasklet_runner_limit(0);

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
            ASSERT_FALSE(enqueue_tasklet2(0, &simple_func2, NULL, NULL));
            break;
         }

         res = enqueue_tasklet2(0, &simple_func2, 1, 2);
         ASSERT_TRUE(res);
         slots_used++;
      }

      c = round(dist(e));

      for (int i = 0; i < c; i++) {

         if (slots_used == 0) {
            ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(0); });
            ASSERT_FALSE(res);
            break;
         }

         ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(0); });
         ASSERT_TRUE(res);
         slots_used--;
      }
   }
}
