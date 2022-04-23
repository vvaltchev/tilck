/* SPDX-License-Identifier: BSD-2-Clause */

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <iostream>
#include <random>

#include "vfs_test.h"

using namespace std;
using namespace testing;

class vfs_misc : public vfs_test_base {

protected:

   struct mnt_fs *fat_fs;
   size_t fatpart_size;

   void SetUp() override {

      vfs_test_base::SetUp();

      const char *buf = load_once_file(TEST_FATPART_FILE, &fatpart_size);
      fat_fs = fat_mount_ramdisk((void *) buf, fatpart_size, 0);
      ASSERT_TRUE(fat_fs != NULL);

      mp_init(fat_fs);
   }

   void TearDown() override {

      fat_umount_ramdisk(fat_fs);
      vfs_test_base::TearDown();
   }
};

TEST_F(vfs_misc, read_content_of_longname_file)
{
   int r;
   char data[128] = {0};

   const char *file_path =
      "/testdir/This_is_a_file_with_a_veeeery_long_name.txt";

   fs_handle h = NULL;
   r = vfs_open(file_path, &h, 0, O_RDONLY);
   ASSERT_TRUE(r == 0);
   ASSERT_TRUE(h != NULL);
   ssize_t res = vfs_read(h, data, sizeof(data));
   vfs_close(h);

   EXPECT_GT(res, 0);
   ASSERT_STREQ("Content of file with a long name\n", data);
}

TEST_F(vfs_misc, fseek)
{
   random_device rdev;
   const auto seed = rdev();
   default_random_engine engine(seed);
   lognormal_distribution<> dist(4.0, 3);
   off_t linux_lseek;
   off_t tilck_fseek;
   off_t linux_pos;
   off_t tilck_pos;

   cout << "[ INFO     ] random seed: " << seed << endl;

   int r;
   const char *fatpart_file_path = "/bigfile";
   const char *real_file_path = PROJ_BUILD_DIR "/test_sysroot/bigfile";

   int fd = open(real_file_path, O_RDONLY);

   size_t file_size = lseek(fd, 0, SEEK_END);

   fs_handle h = NULL;
   r = vfs_open(fatpart_file_path, &h, 0, O_RDONLY);
   ASSERT_TRUE(r == 0);
   ASSERT_TRUE(h != NULL);

   char buf_tilck[64];
   char buf_linux[64];

   linux_lseek = lseek(fd, file_size / 2, SEEK_SET);
   tilck_fseek = vfs_seek(h, file_size / 2, SEEK_SET);
   ASSERT_EQ(linux_lseek, tilck_fseek);

   off_t last_pos = lseek(fd, 0, SEEK_CUR);

   linux_pos = last_pos;
   tilck_pos = vfs_seek(h, 0, SEEK_CUR);
   ASSERT_EQ(linux_pos, tilck_pos);

   (void)last_pos;

   const int iters = 10000;

   for (int i = 0; i < iters; i++) {

      int saved_errno = 0;

      /* random file offset where to seek */
      off_t offset = (off_t) ( dist(engine) - dist(engine)/1.3 );

      linux_lseek = lseek(fd, offset, SEEK_CUR);

      if (linux_lseek < 0)
         saved_errno = errno;

      tilck_fseek = vfs_seek(h, offset, SEEK_CUR);

      linux_pos = lseek(fd, 0, SEEK_CUR);
      tilck_pos = vfs_seek(h, 0, SEEK_CUR);

      if (linux_lseek < 0) {

         /*
          * Linux syscalls return -ERRNO_VALUE in case something goes wrong,
          * while the glibc wrappers return -1 and set errno. Since we're
          * testing the value returned by the syscall in Tilck, we need to
          * revert that.
          */
         linux_lseek = -saved_errno;
      }

      ASSERT_EQ(tilck_fseek, linux_lseek)
         << "Offset: " << offset << endl
         << "Curr pos (glibc): " << linux_pos << endl
         << "Curr pos (tilck):  " << tilck_pos << endl;

      ASSERT_EQ(tilck_pos, linux_pos);

      memset(buf_linux, 0, sizeof(buf_linux));
      memset(buf_tilck, 0, sizeof(buf_tilck));

      ssize_t linux_read = read(fd, buf_linux, sizeof(buf_linux));
      ssize_t tilck_read = vfs_read(h, buf_tilck, sizeof(buf_tilck));

      ASSERT_EQ(tilck_read, linux_read);

      linux_pos = lseek(fd, 0, SEEK_CUR);
      tilck_pos = vfs_seek(h, 0, SEEK_CUR);

      ASSERT_EQ(tilck_pos, linux_pos);

      if (memcmp(buf_tilck, buf_linux, sizeof(buf_linux)) != 0) {

         cout << "Buffers differ. " << endl;
         cout << "Last offset: " << offset << endl;
         cout << "Curr pos: " << linux_pos << endl;
         cout << "read ret: " << linux_read << endl;

         test_dump_buf(buf_linux, "Linux buf:  ", 0, sizeof(buf_linux));
         test_dump_buf(buf_linux, "Tilck buf:  ", 0, sizeof(buf_linux));
         FAIL();
      }

      last_pos = linux_pos;
   }

   vfs_close(h);
   close(fd);
}

class compute_abs_path_test :
   public TestWithParam<
      tuple<const char *, const char *, const char *>
   >
{ };

TEST_P(compute_abs_path_test, check)
{
   const auto &t = GetParam();
   const char *cwd = get<0>(t);
   const char *path = get<1>(t);
   const char *expected = get<2>(t);

   char output[256];
   int rc = compute_abs_path(path, cwd, output, sizeof(output));

   if (rc < 0)
      FAIL() << "Errno: " << rc;

   ASSERT_STREQ(output, expected);
}

INSTANTIATE_TEST_CASE_P(

   abs_path,
   compute_abs_path_test,

   Values(
      make_tuple("/", "/a/b/c", "/a/b/c"),
      make_tuple("/", "/a/b/c/", "/a/b/c/"),
      make_tuple("/", "/a/b/c/..", "/a/b"),
      make_tuple("/", "/a/b/c/../", "/a/b/")
   )
);

INSTANTIATE_TEST_CASE_P(

   rel_path,
   compute_abs_path_test,

   Values(
      make_tuple("/", "a/b/c", "/a/b/c"),
      make_tuple("/", "a/b/c/", "/a/b/c/"),
      make_tuple("/", "a/b/c/..", "/a/b"),
      make_tuple("/", "a/b/c/../", "/a/b/")
   )
);

INSTANTIATE_TEST_CASE_P(

   rel_dot_slash_path,
   compute_abs_path_test,

   Values(
      make_tuple("/", "./a/b/c", "/a/b/c"),
      make_tuple("/", "./a/b/c/", "/a/b/c/"),
      make_tuple("/", "./a/b/c/..", "/a/b"),
      make_tuple("/", "./a/b/c/../", "/a/b/")
   )
);

INSTANTIATE_TEST_CASE_P(

   rel_path_cwd_not_root,
   compute_abs_path_test,

   Values(
      make_tuple("/a/b/c/", "a", "/a/b/c/a"),
      make_tuple("/a/b/c/", "a/", "/a/b/c/a/"),
      make_tuple("/a/b/c/", "..", "/a/b"),
      make_tuple("/a/b/c/", "../", "/a/b/"),
      make_tuple("/a/b/c/", "../..", "/a"),
      make_tuple("/a/b/c/", "../../", "/a/"),
      make_tuple("/a/b/c/", "../../.", "/a"),
      make_tuple("/a/b/c/", "../.././", "/a/"),
      make_tuple("/a/b/c/", "../../..", "/"),
      make_tuple("/a/b/c/", "../../../", "/")
   )
);

INSTANTIATE_TEST_CASE_P(

   try_go_beyond_root,
   compute_abs_path_test,

   Values(
      make_tuple("/a/b/c/", "../../../..", "/"),
      make_tuple("/a/b/c/", "../../../../", "/")
   )
);

INSTANTIATE_TEST_CASE_P(

   multiple_slashes,
   compute_abs_path_test,

   Values(
      make_tuple("/a/b/c/", "d//e", "/a/b/c/d/e"),
      make_tuple("/a/b/c/", "d///e", "/a/b/c/d/e")
   )
);

INSTANTIATE_TEST_CASE_P(

   other,
   compute_abs_path_test,

   Values(
      make_tuple("/a/b/c/", ".a", "/a/b/c/.a"),
      make_tuple("/a/b/c/", "..a", "/a/b/c/..a"),
      make_tuple("/", "something..", "/something.."),
      make_tuple("/", "something.", "/something.")
   )
);
