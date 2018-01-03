
#include <cstdio>
#include <cstring>

#include <iostream>
#include <vector>
#include <random>

using namespace std;

#include <gtest/gtest.h>

#include "kernel_init_funcs.h"

extern "C" {
   #include <fs/fat32.h>
   #include <fs/exvfs.h>
   int check_mountpoint_match(const char *mp, const char *path);
}

// Implemented in fat32_test.cpp
const char *load_once_file(const char *filepath, size_t *fsize = nullptr);

TEST(exvfs, check_mountpoint_match)
{
   EXPECT_EQ(check_mountpoint_match("/", "/"), 1);
   EXPECT_EQ(check_mountpoint_match("/", "/file"), 1);
   EXPECT_EQ(check_mountpoint_match("/", "/dir1/file2"), 1);
   EXPECT_EQ(check_mountpoint_match("/dev/", "/dev/tty0"), 5);
   EXPECT_EQ(check_mountpoint_match("/devices/", "/dev"), 0);
}

TEST(exvfs, read_content_of_longname_file)
{
   initialize_kmalloc_for_tests();

   const char *buf = load_once_file("build/fatpart");
   char data[128] = {0};

   filesystem *fat_fs = fat_mount_ramdisk((void *) buf);
   ASSERT_TRUE(fat_fs != NULL);

   mountpoint_add(fat_fs, "/");

   const char *file_path =
      "/testdir/This_is_a_file_with_a_veeeery_long_name.txt";

   fhandle h = exvfs_open(file_path);
   ASSERT_TRUE(exvfs_is_handle_valid(h));
   ssize_t res = exvfs_read(h, data, sizeof(data));
   exvfs_close(h);

   EXPECT_GT(res, 0);

   ASSERT_STREQ("Content of file with a long name\n", data);
}

TEST(exvfs, fseek)
{
   initialize_kmalloc_for_tests();

   random_device rdev;
   default_random_engine engine(rdev());
   lognormal_distribution<> dist(4.0, 3);

   size_t fatpart_size;
   const char *fatpart = load_once_file("build/fatpart", &fatpart_size);

   filesystem *fat_fs = fat_mount_ramdisk((void *) fatpart);
   ASSERT_TRUE(fat_fs != NULL);

   mountpoint_add(fat_fs, "/");

   const char *fatpart_file_path = "/EFI/BOOT/kernel.bin";
   const char *real_file_path = "build/sysroot/EFI/BOOT/kernel.bin";

   FILE *fp = fopen(real_file_path, "rb");

   fseek(fp, 0, SEEK_END);
   size_t file_size = ftell(fp);

   fhandle h = exvfs_open(fatpart_file_path);
   ASSERT_TRUE(exvfs_is_handle_valid(h));

   char buf_exos[64];
   char buf_glibc[64];

   fseek(fp, file_size / 2, SEEK_SET);
   exvfs_seek(h, file_size / 2, SEEK_SET);

   ssize_t last_pos = ftell(fp);

   for (int i = 0; i < 10000; i++) {

      ssize_t offset = (ssize_t) ( dist(engine) - dist(engine)/2 );

      if (last_pos + offset > file_size) {
         /*
          * Linux's seek() allows the current's file position to go beyond
          * the file's end. ExOS does not allow that for the moment.
          */
         continue;
      }

      long glibc_fseek = fseek(fp, offset, SEEK_CUR);
      long exos_fseek = exvfs_seek(h, offset, SEEK_CUR);

      ssize_t glibc_pos = ftell(fp);
      ssize_t exos_pos = exvfs_tell(h);

      ASSERT_EQ(exos_fseek, glibc_fseek)
         << "Offset: " << offset << endl
         << "Curr pos (glibc): " << glibc_pos << endl
         << "Curr pos (exos):  " << exos_pos << endl;

      ASSERT_EQ(exos_pos, glibc_pos);

      ssize_t glibc_fread = fread(buf_glibc, 1, sizeof(buf_glibc), fp);
      ssize_t exos_fread = exvfs_read(h, buf_exos, sizeof(buf_exos));

      ASSERT_EQ(exos_fread, glibc_fread);

      glibc_pos = ftell(fp);
      exos_pos = exvfs_tell(h);

      ASSERT_EQ(exos_pos, glibc_pos);
      ASSERT_EQ(memcmp(buf_exos, buf_glibc, sizeof(buf_glibc)), 0);

      last_pos = glibc_pos;
   }

   exvfs_close(h);
   fclose(fp);
}
