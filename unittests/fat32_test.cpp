
#include <cstdio>
#include <iostream>
#include <gtest/gtest.h>
#include "kernel_init_funcs.h"

using namespace std;

extern "C" {
   #include <fs/fat32.h>
   #include <fs/exvfs.h>
   #include <utils.h>
}

#define BUFSIZE (40*1024*1024)

TEST(fat32, dumpinfo)
{
   char *buf = (char*) calloc(1, BUFSIZE);

   FILE *fp = fopen("build/fatpart", "rb");
   size_t res = fread(buf, 1, BUFSIZE, fp);
   ASSERT_GT(res, 0);

   fat_dump_info(buf);

   fat_header *hdr = (fat_header*)buf;
   fat_entry *e = fat_search_entry(hdr, fat_unknown, "/nonesistentfile");
   ASSERT_TRUE(e == NULL);

   free(buf);
   fclose(fp);
}

TEST(fat32, read_content_of_shortname_file)
{
   char *buf = (char*) calloc(1, BUFSIZE);
   char data[128] = {0};
   fat_header *hdr;
   fat_entry *e;
   size_t res;
   FILE *fp;

   fp = fopen("build/fatpart", "rb");
   res = fread(buf, 1, BUFSIZE, fp);
   ASSERT_GT(res, 0);
   fclose(fp);

   hdr = (fat_header*)buf;
   e = fat_search_entry(hdr, fat_unknown, "/testdir/dir1/f1");
   ASSERT_TRUE(e != NULL);

   fat_read_whole_file(hdr, e, data, sizeof(data));

   ASSERT_STREQ("hello world!\n", data);

   free(buf);
}

TEST(fat32, read_content_of_longname_file)
{
   char *buf = (char*) calloc(1, BUFSIZE);
   char data[128] = {0};
   fat_header *hdr;
   fat_entry *e;
   size_t res;
   FILE *fp;

   fp = fopen("build/fatpart", "rb");
   res = fread(buf, 1, BUFSIZE, fp);
   ASSERT_GT(res, 0);
   fclose(fp);

   hdr = (fat_header*)buf;
   e = fat_search_entry(hdr,
                        fat_unknown,
                        "/testdir/This_is_a_file_with_a_veeeery_long_name.txt");
   ASSERT_TRUE(e != NULL);
   fat_read_whole_file(hdr, e, data, sizeof(data));

   ASSERT_STREQ("Content of file with a long name\n", data);

   free(buf);
}


TEST(fat32, read_whole_file)
{
   char *buf = (char*) calloc(1, BUFSIZE);

   FILE *fp = fopen("build/fatpart", "rb");
   size_t res = fread(buf, 1, BUFSIZE, fp);
   ASSERT_GT(res, 0);
   fclose(fp);

   fat_header *hdr = (fat_header*)buf;
   fat_entry *e = fat_search_entry(hdr, fat_unknown, "/sbin/init");

   char *content = (char*)calloc(1, e->DIR_FileSize);
   fat_read_whole_file(hdr, e, content, e->DIR_FileSize);

   uint32_t fat_crc = crc32(0, content, e->DIR_FileSize);

   free(content);

   fp = fopen("build/init", "rb");
   res = fread(buf, 1, BUFSIZE, fp);
   ASSERT_EQ(e->DIR_FileSize, res);
   fclose(fp);

   uint32_t actual_file_crc = crc32(0, buf, res);

   ASSERT_EQ(fat_crc, actual_file_crc);
   free(buf);
}

TEST(fat32, fread)
{
   initialize_kmalloc_for_tests();

   char *buf = (char*) calloc(1, BUFSIZE);

   FILE *fp = fopen("build/fatpart", "rb");
   size_t res = fread(buf, 1, BUFSIZE, fp);
   ASSERT_GT(res, 0);
   fclose(fp);

   filesystem *fs = fat_mount_ramdisk(buf);

   fs_handle h = fs->fopen(fs, "/sbin/init");
   ASSERT_TRUE(h != NULL);

   ssize_t buf2_size = 100*1000;
   char *buf2 = (char *) calloc(1, buf2_size);
   ssize_t read_offset = 0;

   fp = fopen("build/init", "rb");
   char tmpbuf[1024];

   while (1) {

      ssize_t read_block_size = 1023; /* 1023 is a prime number */
      ssize_t bytes_read = fs->fread(fs, h, buf2+read_offset, read_block_size);
      ssize_t bytes_read_real_file = fread(tmpbuf, 1, read_block_size, fp);

      ASSERT_EQ(bytes_read_real_file, bytes_read);

      for (int j = 0; j < bytes_read; j++) {
         if (buf2[read_offset+j] != tmpbuf[j]) {

            printf("Byte #%li differs:\n", read_offset+j);

            printf("buf2: ");
            for (int k = 0; k < 16; k++)
               printf("%x ", buf2[read_offset+j+k]);
            printf("\n");
            printf("tmpbuf: ");
            for (int k = 0; k < 16; k++)
               printf("%x ", tmpbuf[j+k]);
            printf("\n");

            FAIL();
         }
      }

      read_offset += bytes_read;

      if (bytes_read < read_block_size)
         break;
   }

   fclose(fp);
   fs->fclose(fs, h);

   fat_umount_ramdisk(fs);
   free(buf2);
   free(buf);
}
