/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "mocking.h"

using namespace std;
using namespace testing;

/*
 * Mocking of C functions in the kernel
 *
 * Instructions:
 *
 *    1. add all the functions that we would like to mock in the WRAPPED_SYMS
 *       list in other/cmake/wrapped_syms.cmake
 *
 *    2. add all of those functions in tests/unit/mocked_funcs.h, with their
 *       correct signature
 *
 *    3. create gMOCK classes with one or more mocked "methods" like MockingBar
 *
 *    4. instantiate ONE mock object per TEST and use it with EXPECT_CALL or
 *       ON_CALL, as explained in the gmock documentation
 */

extern "C" {
bool experiment_bar();
int experiment_foo(int);
}

class MockingBar : public KernelSingleton {
public:

   MOCK_METHOD(bool, experiment_bar, (), (override));
};


/*
 * Base case: call the functions without any mocking, and expect them to work.
 */
TEST(experiment, gfuncs1)
{
   ASSERT_EQ(experiment_bar(), true);
   ASSERT_EQ(experiment_foo(5), 50);
   ASSERT_EQ(experiment_foo(6), 60);
}

/*
 * Basic mocking: mock experiment_bar() to first fail and then to succeed.
 * Note: experiment_foo() remains in its original form, despite the jump
 * through KernelSingleton's vtable.
 */
TEST(experiment, gfuncs2)
{
   MockingBar mock;

   EXPECT_CALL(mock, experiment_bar)
      .WillOnce(Return(false))
      .WillOnce(Return(true));

   ASSERT_EQ(experiment_foo(5), -1);
   ASSERT_EQ(experiment_foo(5), 50);
}
