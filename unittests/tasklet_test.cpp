
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
      initialize_test_kernel_heap();
      init_pageframe_allocator();
      initialize_kmalloc();
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


TEST_F(tasklet_test, base)
{
   bool res;

   for (int i = 0; i < MAX_TASKLETS; i++) {
      res = add_tasklet((void *)&simple_func, (void*)1, (void*)2, (void*)3);
      ASSERT_TRUE(res);
   }

   res = add_tasklet((void *)&simple_func, (void*)1, (void*)2, (void*)3);

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
      res = add_tasklet((void *)&simple_func, (void*)1, (void*)2, (void*)3);
      ASSERT_TRUE(res);
   }

   // Consume 1/4.
   for (int i = 0; i < MAX_TASKLETS/4; i++) {
      ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(); });
      ASSERT_TRUE(res);
   }

   // Fill half of the buffer.
   for (int i = 0; i < MAX_TASKLETS/2; i++) {
      res = add_tasklet((void *)&simple_func, (void*)1, (void*)2, (void*)3);
      ASSERT_TRUE(res);
   }

   // Consume 2/4
   for (int i = 0; i < MAX_TASKLETS/2; i++) {
      ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(); });
      ASSERT_TRUE(res);
   }

   // Fill half of the buffer.
   for (int i = 0; i < MAX_TASKLETS/2; i++) {
      res = add_tasklet((void *)&simple_func, (void*)1, (void*)2, (void*)3);
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

TEST_F(tasklet_test, chaos_test)
{
   random_device rdev;
   default_random_engine e(rdev());

   lognormal_distribution<> dist(3.0, 2.5);

   int slots_used = 0;
   bool res;

   for (int iters = 0; iters < 10000; iters++) {

      int c;
      c = round(dist(e));

      for (int i = 0; i < c; i++) {

         if (slots_used == MAX_TASKLETS) {
            ASSERT_FALSE(add_tasklet((void *)&simple_func, NULL, NULL, NULL));
            break;
         }

         res = add_tasklet((void *)&simple_func, (void*)1, (void*)2, (void*)3);
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
