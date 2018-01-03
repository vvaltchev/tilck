
#include <cstdio>
#include <iostream>
#include <vector>

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
