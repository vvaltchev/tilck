/* SPDX-License-Identifier: BSD-2-Clause */

#include "vfs_test.h"

using namespace std;

class ramfs_perf : public vfs_test_base {

protected:
   filesystem *fs;

   void SetUp() override {

      vfs_test_base::SetUp();

      fs = ramfs_create();
      ASSERT_TRUE(fs != NULL);
      mp2_init(fs);
   }

   void TearDown() override {

      // TODO: destroy ramfs
      vfs_test_base::TearDown();
   }
};


static void create_test_file(int n)
{
   char path[256];
   fs_handle h;
   int rc;

   sprintf(path, "/test_%d", n);

   rc = vfs_open(path, &h, O_CREAT, 0644);
   ASSERT_EQ(rc, 0);

   vfs_close(h);
}

TEST_F(ramfs_perf, creat)
{
   for (int i = 0; i < 100; i++)
      create_test_file(i);
}
