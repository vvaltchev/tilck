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

   MOCK_METHOD(int,
               copy_str_from_user,
               (void *, const void *, size_t, size_t *),
               (override));

   MOCK_METHOD(int,
               copy_from_user,
               (void *, const void *, size_t),
               (override));
};

TEST(tracer_test, save_param_buffer)
{
   MockingTracer mock;

   long data_sz = -1;
   void *data = (void *)"test";

   char dest_buf_1[8];
   const size_t dest_bs_1 = sizeof(dest_buf_1);

   char dest_buf_2[8];
   const size_t dest_bs_2 = sizeof(dest_buf_2);

   char dest_buf_3[8];
   const size_t dest_bs_3 = sizeof(dest_buf_3);

   EXPECT_CALL(mock, copy_str_from_user)
      .WillOnce(Return(-1))
      .WillOnce([] (void *dest, const void *user_ptr, size_t, size_t *) {
            strcpy((char *)dest, (char *)user_ptr);
            return 1;
         });

   // rc < 0
   EXPECT_TRUE(save_param_buffer(data, data_sz, dest_buf_1, dest_bs_1));
   EXPECT_STREQ(dest_buf_1, "<fault>");

   // rc > 0
   void *data_2 = (void *)"VeryVeryLong";

   EXPECT_TRUE(save_param_buffer(data_2, data_sz, dest_buf_2, dest_bs_2));
   EXPECT_STREQ(dest_buf_2, "VeryVer");

   // data_sz >= 0
   data_sz = 5;

   EXPECT_CALL(mock, copy_from_user)
      .WillOnce(Return(1));

   EXPECT_TRUE(save_param_buffer(data, data_sz, dest_buf_3, dest_bs_3));
   EXPECT_STREQ(dest_buf_3, "<fault>");
}

TEST(tracer_test, dump_param_buffer)
{
   MockingTracer mock;

   ulong orig = 1;
   long data_bs = -1;
   long real_sz = -1;

   char *data_1 = (char *)"\r";
   char dest_1[10];
   const size_t dest_bs_1 = sizeof(dest_1);

   char *data_2 = (char *)"\"";
   char dest_2[10];
   const size_t dest_bs_2 = sizeof(dest_2);

   char *data_3 = (char *)"\\";
   char dest_3[10];
   const size_t dest_bs_3 = sizeof(dest_3);

   EXPECT_TRUE(
      dump_param_buffer(orig, data_1, data_bs, real_sz, dest_1, dest_bs_1)
   );
   EXPECT_STREQ(dest_1, "\"\\r\"");

   EXPECT_TRUE(
      dump_param_buffer(orig, data_2, data_bs, real_sz, dest_2, dest_bs_2)
   );
   EXPECT_STREQ(dest_2, "\"\\\"\"");

   EXPECT_TRUE(
      dump_param_buffer(orig, data_3, data_bs, real_sz, dest_3, dest_bs_3)
   );
   EXPECT_STREQ(dest_3, "\"\\\\\"");

   // For `if (dest_end - dest < ml - 1)` path
   // and `if (dest >= dest_end - 4)` path
   char *data_4 = (char *)"VeryVeryLong";
   char dest_4[10];
   const size_t dest_bs_4 = sizeof(dest_4);

   EXPECT_TRUE(
      dump_param_buffer(orig, data_4, data_bs, real_sz, dest_4, dest_bs_4)
   );
   EXPECT_STREQ(dest_4, "\"Very...\"");

   // For `if (s == data_end && real_sz > 0 && data_bs < real_sz)` path
   char *data_5 = (char *)"abcd";
   char dest_5[10];
   const size_t dest_bs_5 = sizeof(dest_5);

   real_sz = 2;

   EXPECT_TRUE(
      dump_param_buffer(orig, data_5, data_bs, real_sz, dest_5, dest_bs_5)
   );
   EXPECT_STREQ(dest_5, "\"ab\"");
}
