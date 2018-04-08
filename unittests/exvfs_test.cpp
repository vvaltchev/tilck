
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <cstdio>
#include <cstring>

#include <iostream>
#include <vector>
#include <random>

using namespace std;

#include <gtest/gtest.h>

#include "kernel_init_funcs.h"

extern "C" {
   #include <exos/fs/fat32.h>
   #include <exos/fs/exvfs.h>
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
   init_kmalloc_for_tests();

   const char *buf = load_once_file("build/fatpart");
   char data[128] = {0};

   filesystem *fat_fs = fat_mount_ramdisk((void *) buf);
   ASSERT_TRUE(fat_fs != NULL);

   int r = mountpoint_add(fat_fs, "/");
   ASSERT_EQ(r, 0);

   const char *file_path =
      "/testdir/This_is_a_file_with_a_veeeery_long_name.txt";

   fs_handle h = exvfs_open(file_path);
   ASSERT_TRUE(h != NULL);
   ssize_t res = exvfs_read(h, data, sizeof(data));
   exvfs_close(h);

   EXPECT_GT(res, 0);
   ASSERT_STREQ("Content of file with a long name\n", data);

   mountpoint_remove(fat_fs);
   fat_umount_ramdisk(fat_fs);
}

TEST(exvfs, fseek)
{
   init_kmalloc_for_tests();

   random_device rdev;
   const auto seed = rdev();
   default_random_engine engine(seed);
   lognormal_distribution<> dist(4.0, 3);

   cout << "[ INFO     ] random seed: " << seed << endl;

   size_t fatpart_size;
   const char *fatpart = load_once_file("build/fatpart", &fatpart_size);

   filesystem *fat_fs = fat_mount_ramdisk((void *) fatpart);
   ASSERT_TRUE(fat_fs != NULL);

   int r = mountpoint_add(fat_fs, "/");
   ASSERT_EQ(r, 0);

   const char *fatpart_file_path = "/EFI/BOOT/elf_kernel_stripped";
   const char *real_file_path = "build/sysroot/EFI/BOOT/elf_kernel_stripped";

   int fd = open(real_file_path, O_RDONLY);

   size_t file_size = lseek(fd, 0, SEEK_END);

   fs_handle h = exvfs_open(fatpart_file_path);
   ASSERT_TRUE(h != NULL);

   char buf_exos[64];
   char buf_linux[64];

   lseek(fd, file_size / 2, SEEK_SET);
   exvfs_seek(h, file_size / 2, SEEK_SET);

   off_t last_pos = lseek(fd, 0, SEEK_CUR);

   const int iters = 10000;

   for (int i = 0; i < iters; i++) {

      int saved_errno = 0;
      off_t offset = (off_t) ( dist(engine) - dist(engine)/1.3 );

      off_t linux_lseek = lseek(fd, offset, SEEK_CUR);
      off_t exos_fseek = exvfs_seek(h, offset, SEEK_CUR);

      if (linux_lseek < 0)
         saved_errno = errno;

      off_t linux_pos = lseek(fd, 0, SEEK_CUR);
      off_t exos_pos = exvfs_seek(h, 0, SEEK_CUR);

      if (linux_lseek < 0) {

         /*
          * Linux syscalls return -ERRNO_VALUE in case something goes wrong,
          * while the glibc wrappers return -1 and set errno. Since we're
          * testing the value returned by the syscall in exOS, we need to revert
          * that.
          */
         linux_lseek = -errno;
      }

      ASSERT_EQ(exos_fseek, linux_lseek)
         << "Offset: " << offset << endl
         << "Curr pos (glibc): " << linux_pos << endl
         << "Curr pos (exos):  " << exos_pos << endl;

      ASSERT_EQ(exos_pos, linux_pos);

      memset(buf_linux, 0, sizeof(buf_linux));
      memset(buf_exos, 0, sizeof(buf_exos));

      ssize_t linux_read = read(fd, buf_linux, sizeof(buf_linux));
      ssize_t exos_read = exvfs_read(h, buf_exos, sizeof(buf_exos));

      ASSERT_EQ(exos_read, linux_read);

      linux_pos = lseek(fd, 0, SEEK_CUR);
      exos_pos = exvfs_seek(h, 0, SEEK_CUR);

      ASSERT_EQ(exos_pos, linux_pos);

      if (memcmp(buf_exos, buf_linux, sizeof(buf_linux)) != 0) {

         cout << "Buffers differ. " << endl;
         cout << "Last offset: " << offset << endl;
         cout << "Curr pos: " << linux_pos << endl;
         cout << "read ret: " << linux_read << endl;

         cout << "Linux buf: ";

         for (size_t i = 0; i < sizeof(buf_linux); i++)
            printf("%02x ", (u8)buf_linux[i]);

         cout << endl;
         cout << "ExOS buf:  ";

         for (size_t i = 0; i < sizeof(buf_linux); i++)
            printf("%02x ", (u8)buf_exos[i]);

         cout << endl;
         FAIL();
      }

      last_pos = linux_pos;
   }

   exvfs_close(h);
   close(fd);

   mountpoint_remove(fat_fs);
   fat_umount_ramdisk(fat_fs);
}
