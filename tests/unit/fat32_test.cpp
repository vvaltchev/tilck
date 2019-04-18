/* SPDX-License-Identifier: BSD-2-Clause */

#include <cstdio>
#include <iostream>
#include <memory>
#include <map>
#include <gtest/gtest.h>

#include "kernel_init_funcs.h"

extern "C" {
   #include <tilck/kernel/fs/fat32.h>
   #include <tilck/kernel/fs/vfs.h>
   #include <tilck/common/utils.h>
}

using namespace std;

void test_dump_buf(char *buf, const char *buf_name, int off, int count)
{
   printf("%s", buf_name);

   for (int k = 0; k < count; k++)
      printf("%02x ", (unsigned char) buf[off + k]);

   printf("\n");
}

static void
fat32_read_compare_bufs(ssize_t bytes_read, ssize_t roff, char *b1, char *b2)
{
   for (int j = 0; j < bytes_read; j++) {
      if (b1[j] != b2[j]) {
         printf("Byte #%li differs:\n", (long)roff+j);
         test_dump_buf(b1, "buf1: ", j, 16);
         test_dump_buf(b2, "buf2: ", j, 16);
         FAIL();
      }
   }
}

const char *load_once_file(const char *filepath, size_t *fsize = nullptr)
{
   static map<const char *,
              pair<unique_ptr<const char[]>, size_t>> files_loaded;

   auto it = files_loaded.find(filepath);

   if (it == files_loaded.end()) {

      long file_size;
      char *buf;
      FILE *fp;

      fp = fopen(filepath, "rb");
      assert(fp != nullptr);

      fseek(fp, 0, SEEK_END);
      file_size = ftell(fp);

      buf = new char [file_size];
      assert(buf != nullptr);

      fseek(fp, 0, SEEK_SET);
      ssize_t bytes_read = fread(buf, 1, file_size, fp);
      assert(bytes_read == file_size);
      (void)bytes_read;

      fclose(fp);

      auto &e = files_loaded[filepath];
      e.first.reset(buf);
      e.second = file_size;
   }

   auto &e = files_loaded[filepath];

   if (fsize)
      *fsize = e.second;

   return e.first.get();
}

TEST(fat32, DISABLED_dumpinfo)
{
   const char *buf = load_once_file(PROJ_BUILD_DIR "/test_fatpart");
   fat_dump_info((void *) buf);

   fat_header *hdr = (fat_header*)buf;
   fat_entry *e = fat_search_entry(hdr, fat_unknown, "/nonesistentfile", NULL);
   ASSERT_TRUE(e == NULL);
}

TEST(fat32, read_content_of_shortname_file)
{
   const char *buf = load_once_file(PROJ_BUILD_DIR "/test_fatpart");
   char data[128] = {0};
   fat_header *hdr;
   fat_entry *e;

   hdr = (fat_header *)buf;
   e = fat_search_entry(hdr, fat_unknown, "/testdir/dir1/f1", NULL);
   ASSERT_TRUE(e != NULL);

   fat_read_whole_file(hdr, e, data, sizeof(data));

   ASSERT_STREQ("hello world!\n", data);
}

TEST(fat32, read_content_of_longname_file)
{
   const char *buf = load_once_file(PROJ_BUILD_DIR "/test_fatpart");
   char data[128] = {0};
   fat_header *hdr;
   fat_entry *e;

   hdr = (fat_header *)buf;

   e = fat_search_entry(hdr,
                        fat_unknown,
                        "/testdir/This_is_a_file_with_a_veeeery_long_name.txt",
                        NULL);

   ASSERT_TRUE(e != NULL);
   fat_read_whole_file(hdr, e, data, sizeof(data));

   ASSERT_STREQ("Content of file with a long name\n", data);
}

TEST(fat32, read_whole_file)
{
   fat_header *hdr = (fat_header *)
      load_once_file(PROJ_BUILD_DIR "/test_fatpart");

   fat_entry *e = fat_search_entry(hdr, fat_unknown, "/bigfile", NULL);

   char *content = (char *)calloc(1, e->DIR_FileSize);
   fat_read_whole_file(hdr, e, content, e->DIR_FileSize);
   uint32_t fat_crc = crc32(0, content, e->DIR_FileSize);
   free(content);

   size_t fsize;
   const char *buf =
      load_once_file(PROJ_BUILD_DIR "/test_sysroot/bigfile", &fsize);
   uint32_t actual_file_crc = crc32(0, buf, fsize);
   ASSERT_EQ(fat_crc, actual_file_crc);
}

TEST(fat32, fread)
{
   char buf1[1024];
   char buf2[1024];
   fs_handle h = NULL;
   const ssize_t block_size = 1023; /* prime number */
   ssize_t bytes_read, bytes_read_real_file, read_offset = 0;
   int rc;

   init_kmalloc_for_tests();

   filesystem *fs =
      fat_mount_ramdisk(
         (void *) load_once_file(PROJ_BUILD_DIR "/test_fatpart"),
         VFS_FS_RO);

   ASSERT_TRUE(fs != NULL);

   rc = fs->fsops->open(fs, "/bigfile", &h, 0, O_RDONLY);

   ASSERT_TRUE(rc == 0);
   ASSERT_TRUE(h != NULL);

   fat_file_handle *fat_handle = (fat_file_handle *)h;
   FILE *fp = fopen(PROJ_BUILD_DIR "/test_sysroot/bigfile", "rb");

   do {

      bytes_read = fat_handle->fops->read(h, buf1, block_size);
      bytes_read_real_file = fread(buf2, 1, block_size, fp);

      ASSERT_EQ(bytes_read_real_file, bytes_read);

      ASSERT_NO_FATAL_FAILURE({
         fat32_read_compare_bufs(bytes_read, read_offset, buf1, buf2);
      });

      read_offset += bytes_read;

   } while (bytes_read == block_size);

   fclose(fp);
   fs->fsops->close(h);
   fat_umount_ramdisk(fs);
}
