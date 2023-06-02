/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "mocking.h"

using namespace testing;

extern "C" {
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/user.h>
#include <tilck/mods/tracing.h>

#include <tilck/kernel/test/tracing.h>
}

class MockingTracer : public KernelSingleton {
public:

   MOCK_METHOD(int, copy_str_from_user, (void *, const void *, size_t, size_t *), (override));
   MOCK_METHOD(int, copy_from_user, (void *, const void *, size_t), (override));
};

TEST(tracer_test, save_param_buffer)
{
   MockingTracer mock;

   long data_sz = -1;
   void *data = (void *)"test";

   char dest_buf_1[8];
   size_t dest_bs_1 = sizeof(dest_buf_1);

   char dest_buf_2[8];
   size_t dest_bs_2 = sizeof(dest_buf_2);

   char dest_buf_3[8];
   size_t dest_bs_3 = sizeof(dest_buf_3);

   EXPECT_CALL(mock, copy_str_from_user)
      .WillOnce(Return(-1))
      .WillOnce(Return(1));

   // rc < 0
   EXPECT_TRUE(save_param_buffer(data, data_sz, dest_buf_1, dest_bs_1));
   EXPECT_STREQ(dest_buf_1, "<fault>");

   // rc > 0
   EXPECT_TRUE(save_param_buffer(data, data_sz, dest_buf_2, dest_bs_2));
   EXPECT_EQ(dest_buf_2[dest_bs_2 - 1], 0);

   data_sz = 0;

   EXPECT_CALL(mock, copy_from_user)
      .WillOnce(Return(1));

   EXPECT_TRUE(save_param_buffer(data, data_sz, dest_buf_3, dest_bs_3));
   EXPECT_STREQ(dest_buf_3, "<fault>");
}
