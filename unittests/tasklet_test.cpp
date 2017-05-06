
#include <common_defs.h>

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
   #include <tasklet.h>
}

using namespace std;
using namespace testing;

class tasklet_test : public Test {

   void SetUp() override {
      initialize_kmalloc_for_tests();
      initialize_tasklets();
   }

   void TearDown() override {
      /* do nothing, for the moment */
   }
};

void simple_func(void *p1, void *p2, void *p3)
{
   ASSERT_EQ(p1, (void*) 1);
   ASSERT_EQ(p2, (void*) 2);
   ASSERT_EQ(p3, (void*) 3);
}

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

   ASSERT_TRUE(add_tasklet3(&simple_func, 1, 2, 3));
   ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(); });
   ASSERT_TRUE(res);

   ASSERT_TRUE(add_tasklet2(&simple_func2, 1, 2));
   ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(); });
   ASSERT_TRUE(res);

   ASSERT_TRUE(add_tasklet1(&simple_func1, 1));
   ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(); });
   ASSERT_TRUE(res);
}


TEST_F(tasklet_test, base)
{
   bool res;

   for (int i = 0; i < MAX_TASKLETS; i++) {
      res = add_tasklet3(&simple_func, 1, 2, 3);
      ASSERT_TRUE(res);
   }

   res = add_tasklet3(&simple_func, 1, 2, 3);

   // There is no more space left, expecting the ADD failed.
   ASSERT_FALSE(res);

   for (int i = 0; i < MAX_TASKLETS; i++) {
      ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(); });
      ASSERT_TRUE(res);
   }

   ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(); });

   // There are no more tasklets, expecting the RUN failed.
   ASSERT_FALSE(res);
}


TEST_F(tasklet_test, advanced)
{
   bool res;

   // Fill half of the buffer.
   for (int i = 0; i < MAX_TASKLETS/2; i++) {
      res = add_tasklet3(&simple_func, 1, 2, 3);
      ASSERT_TRUE(res);
   }

   // Consume 1/4.
   for (int i = 0; i < MAX_TASKLETS/4; i++) {
      ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(); });
      ASSERT_TRUE(res);
   }

   // Fill half of the buffer.
   for (int i = 0; i < MAX_TASKLETS/2; i++) {
      res = add_tasklet3(&simple_func, 1, 2, 3);
      ASSERT_TRUE(res);
   }

   // Consume 2/4
   for (int i = 0; i < MAX_TASKLETS/2; i++) {
      ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(); });
      ASSERT_TRUE(res);
   }

   // Fill half of the buffer.
   for (int i = 0; i < MAX_TASKLETS/2; i++) {
      res = add_tasklet3(&simple_func, 1, 2, 3);
      ASSERT_TRUE(res);
   }

   // Now the cyclic buffer for sure rotated.

   // Consume 3/4
   for (int i = 0; i < 3*MAX_TASKLETS/4; i++) {
      ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(); });
      ASSERT_TRUE(res);
   }

   ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(); });

   // There are no more tasklets, expecting the RUN failed.
   ASSERT_FALSE(res);
}

TEST_F(tasklet_test, chaos)
{
   random_device rdev;
   default_random_engine e(rdev());

   lognormal_distribution<> dist(3.0, 2.5);

   int slots_used = 0;
   bool res = false;

   for (int iters = 0; iters < 10000; iters++) {

      int c;
      c = round(dist(e));

      for (int i = 0; i < c; i++) {

         if (slots_used == MAX_TASKLETS) {
            ASSERT_FALSE(add_tasklet3(&simple_func, NULL, NULL, NULL));
            break;
         }

         res = add_tasklet3(&simple_func, 1, 2, 3);
         ASSERT_TRUE(res);
         slots_used++;
      }

      c = round(dist(e));

      for (int i = 0; i < c; i++) {

         if (slots_used == 0) {
            ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(); });
            ASSERT_FALSE(res);
            break;
         }

         ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(); });
         ASSERT_TRUE(res);
         slots_used--;
      }
   }
}
