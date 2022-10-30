/* SPDX-License-Identifier: BSD-2-Clause */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "mocking.h"

using namespace testing;

extern "C" {
   #include <tilck/kernel/process.h>
   #include <tilck/kernel/test/fork.h>
}

class vfs_mock : public KernelSingleton {
public:

   MOCK_METHOD(int, vfs_dup, (fs_handle h, fs_handle *dup_h), (override));
   MOCK_METHOD(void, vfs_close, (fs_handle h), (override));
};

TEST(fork_dup_all_handles, trigger_inside_path)
{
   vfs_mock mock;
   process pi = {};
   fs_handle_base handles[3] = {}, dup_handles[2] = {};
   pi.handles[0] = &handles[0];
   pi.handles[1] = &handles[1];
   pi.handles[2] = &handles[2];

   EXPECT_CALL(mock, vfs_dup(&handles[0], _))
      .WillOnce(
         DoAll(
            SetArgPointee<1>(&dup_handles[0]),
            Return(0)
         )
      );
   EXPECT_CALL(mock, vfs_dup(&handles[1], _))
      .WillOnce(
         DoAll(
            SetArgPointee<1>(&dup_handles[1]),
            Return(0)
         )
      );
   EXPECT_CALL(mock, vfs_dup(&handles[2], _))
      .WillOnce(Return(-1));

   EXPECT_CALL(mock, vfs_close(&dup_handles[0]));
   EXPECT_CALL(mock, vfs_close(&dup_handles[1]));
   ASSERT_EQ(fork_dup_all_handles(&pi), -ENOMEM);
}
