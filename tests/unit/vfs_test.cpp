/* SPDX-License-Identifier: BSD-2-Clause */

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <iostream>
#include <random>

#include "vfs_test.h"

using namespace std;
using namespace testing;

class vfs_fat32 : public vfs_test_base {

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

TEST_F(vfs_fat32, read_content_of_longname_file)
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

TEST_F(vfs_fat32, fseek)
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
         FAIL() << "memcmp failed";
      }

      last_pos = linux_pos;
   }

   vfs_close(h);
   close(fd);
}

TEST_F(vfs_fat32, pread_not_supported)
{
   const char *fatpart_file_path = "/bigfile";
   fs_handle h = NULL;
   char buf[32];
   int rc;

   rc = vfs_open(fatpart_file_path, &h, 0, O_RDONLY);
   ASSERT_TRUE(rc == 0);
   ASSERT_TRUE(h != NULL);

   rc = vfs_pread(h, buf, sizeof(buf), 0 /* offset */);
   EXPECT_EQ(rc, -EPERM);

   rc = vfs_pread(h, buf, sizeof(buf), 10 /* offset */);
   EXPECT_EQ(rc, -EPERM);

   vfs_close(h);
}

class vfs_ramfs : public vfs_test_base {

protected:
   struct mnt_fs *mnt_fs;

   void SetUp() override {

      vfs_test_base::SetUp();

      mnt_fs = ramfs_create();
      ASSERT_TRUE(mnt_fs != NULL);
      mp_init(mnt_fs);
   }

   void TearDown() override {

      // TODO: destroy ramfs
      vfs_test_base::TearDown();
   }

   /* Custom test helper functions */
   void test_pread_pwrite_seek(bool fseek);
};

TEST_F(vfs_ramfs, create_and_unlink_file)
{
   static const char my_data[] = "hello world";

   fs_handle h;
   int rc;
   char buf[32];

   rc = vfs_open("/file1", &h, O_CREAT | O_RDWR, 0644);
   ASSERT_EQ(rc, 0);

   rc = vfs_write(h, (void *)my_data, sizeof(my_data));
   ASSERT_EQ(rc, (int)sizeof(my_data));

   vfs_close(h);

   rc = vfs_open("/file1", &h, O_RDWR, 0644);
   ASSERT_EQ(rc, 0);

   rc = vfs_read(h, buf, sizeof(buf));
   ASSERT_EQ(rc, (int)sizeof(my_data));

   ASSERT_STREQ(buf, my_data);
   vfs_close(h);

   rc = vfs_unlink("/file1");
   ASSERT_EQ(rc, 0);

   rc = vfs_open("/file1", &h, O_RDWR, 0);
   ASSERT_EQ(rc, -ENOENT);
}

void vfs_ramfs::test_pread_pwrite_seek(bool fseek)
{
   const off_t data_size = 2 * MB;
   const int iters = 10000;
   const char *const path = "/bigfile_random_data";
   constexpr size_t buf_size = 64;

   random_device rdev;
   const auto seed = rdev();
   default_random_engine engine(seed);
   lognormal_distribution<> off_dist(4.0, 3);
   uniform_int_distribution<uint8_t> data_dist(0, 255);
   uniform_int_distribution<uint8_t> op_dist(0, 1);

   fs_handle h;
   char buf[buf_size] = {0};
   vector<uint8_t> data;
   char *data_buf;
   off_t rc, curr_pos = 0;

   data.reserve(data_size);

   cout << "[ INFO     ] random seed: " << seed << endl;

   for (int i = 0; i < data_size; i++) {
      data.push_back(data_dist(engine));
   }

   ASSERT_EQ(data.size(), (size_t)data_size);
   data_buf = (char *)&data[0];

   rc = vfs_open(path, &h, O_CREAT | O_RDWR, 0644);
   ASSERT_EQ(rc, 0);

   auto clean_up = [&]() {

      if (h) {
         vfs_close(h);

         rc = vfs_unlink(path);
         ASSERT_EQ(rc, 0);

         rc = vfs_open(path, &h, O_RDWR, 0);
         ASSERT_EQ(rc, -ENOENT);
      }
   };

   {
      off_t written = 0;

      while (written < data_size) {

         rc = vfs_write(h, data_buf + written, data_size - written);

         if (rc <= 0) {
            clean_up();
            FAIL() << "initial vfs_write() failed with: " << rc;
         }

         written += rc;
      }
   }

   vfs_close(h);

   rc = vfs_open(path, &h, O_RDWR, 0644);
   ASSERT_EQ(rc, 0);

   for (int i = 0; i < iters; i++) {

      const uint8_t op = op_dist(engine);
      const off_t off = (off_t) ( off_dist(engine) - off_dist(engine)/1.3 );
      const off_t target_pos = off + curr_pos;

      if (target_pos < 0 || target_pos >= data_size) {
         i--;
         continue; /* invalid offset, re-try the iteration */
      }

      const off_t rem = data_size - target_pos;

      if (fseek) {

         /* Change the offset with a seek operation */
         off_t res = vfs_seek(h, off, SEEK_CUR);

         if (res < 0) {
            clean_up();
            FAIL() << "vfs_seek() failed with: " << res;
         }

         if (res != target_pos) {
            clean_up();
            ASSERT_EQ(res, target_pos);
         }

         if ((res = vfs_seek(h, 0, SEEK_CUR)) != target_pos) {
            clean_up();
            FAIL() << "pos(" << res << ") != target_pos(" << target_pos << ")";
         }

         if ((res = vfs_seek(h, target_pos, SEEK_SET)) != target_pos) {
            clean_up();
            FAIL() << "pos(" << res << ") != target_pos(" << target_pos << ")";
         }

         curr_pos = target_pos;
      }

      if (op == 0) {

         /* read */
         off_t to_read = min((off_t)sizeof(buf), rem);

         if (fseek) {
            rc = vfs_read(h, buf, to_read);
         } else {
            rc = vfs_pread(h, buf, to_read, target_pos);
         }

         if (rc < 0) {
            clean_up();
            FAIL() << "vfs_read() failed with: " << rc;
         }

         if (rc != to_read) {
            clean_up();
            ASSERT_EQ(rc, to_read);
         }

         /* Compare the data in the mem buffer with what we read from VFS */
         if (memcmp(data_buf + target_pos, buf, to_read) != 0) {
            clean_up();
            test_dump_buf(data_buf, "Expected: ", target_pos, to_read);
            test_dump_buf(buf,      "Read:     ", 0, to_read);
            FAIL() << "memcmp failed";
         }

         if (fseek)
            curr_pos += rc;

      } else {

         /* write */
         off_t to_write = min((off_t)sizeof(buf), rem);

         if (fseek) {
            rc = vfs_write(h, buf, to_write);
         } else {
            rc = vfs_pwrite(h, buf, to_write, target_pos);
         }

         if (rc < 0) {
            clean_up();
            FAIL() << "vfs_write() failed with: " << rc;
         }

         if (rc != to_write) {
            clean_up();
            ASSERT_EQ(rc, to_write);
         }

         /* Write the new data on the curr_pos in our mem buffer */
         memmove(data_buf + target_pos, buf, to_write);

         if (fseek)
            curr_pos += rc;
      }
   }

   clean_up();
}

TEST_F(vfs_ramfs, pread_pwrite)
{
   ASSERT_NO_FATAL_FAILURE({ test_pread_pwrite_seek(false); });
}

TEST_F(vfs_ramfs, seek)
{
   ASSERT_NO_FATAL_FAILURE({ test_pread_pwrite_seek(true); });
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

INSTANTIATE_TEST_SUITE_P(

   abs_path,
   compute_abs_path_test,

   Values(
      make_tuple("/", "/a/b/c", "/a/b/c"),
      make_tuple("/", "/a/b/c/", "/a/b/c/"),
      make_tuple("/", "/a/b/c/..", "/a/b"),
      make_tuple("/", "/a/b/c/../", "/a/b/")
   )
);

INSTANTIATE_TEST_SUITE_P(

   rel_path,
   compute_abs_path_test,

   Values(
      make_tuple("/", "a/b/c", "/a/b/c"),
      make_tuple("/", "a/b/c/", "/a/b/c/"),
      make_tuple("/", "a/b/c/..", "/a/b"),
      make_tuple("/", "a/b/c/../", "/a/b/")
   )
);

INSTANTIATE_TEST_SUITE_P(

   rel_dot_slash_path,
   compute_abs_path_test,

   Values(
      make_tuple("/", "./a/b/c", "/a/b/c"),
      make_tuple("/", "./a/b/c/", "/a/b/c/"),
      make_tuple("/", "./a/b/c/..", "/a/b"),
      make_tuple("/", "./a/b/c/../", "/a/b/")
   )
);

INSTANTIATE_TEST_SUITE_P(

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

INSTANTIATE_TEST_SUITE_P(

   try_go_beyond_root,
   compute_abs_path_test,

   Values(
      make_tuple("/a/b/c/", "../../../..", "/"),
      make_tuple("/a/b/c/", "../../../../", "/")
   )
);

INSTANTIATE_TEST_SUITE_P(

   multiple_slashes,
   compute_abs_path_test,

   Values(
      make_tuple("/a/b/c/", "d//e", "/a/b/c/d/e"),
      make_tuple("/a/b/c/", "d///e", "/a/b/c/d/e")
   )
);

INSTANTIATE_TEST_SUITE_P(

   other,
   compute_abs_path_test,

   Values(
      make_tuple("/a/b/c/", ".a", "/a/b/c/.a"),
      make_tuple("/a/b/c/", "..a", "/a/b/c/..a"),
      make_tuple("/", "something..", "/something.."),
      make_tuple("/", "something.", "/something.")
   )
);
