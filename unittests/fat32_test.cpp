
#include <cstdio>
#include <iostream>
#include <gtest/gtest.h>

using namespace std;

extern "C" {
   #include <fs/fat32.h>
   #include <fs/exvfs.h>
   #include <utils.h>
}

#define BUFSIZE (40*1024*1024)

TEST(fat32, dumpinfo)
{
   char *buf;
   FILE *fd = fopen("build/fatpart", "rb");

   buf = (char*) calloc(1, BUFSIZE);
   size_t res = fread(buf, 1, BUFSIZE, fd);
   ASSERT_GT(res, 0);

   fat_dump_info(buf);

   fat_header *hdr = (fat_header*)buf;
   fat_entry *e = fat_search_entry(hdr, fat_unknown, "/nonesistentfile");
   ASSERT_TRUE(e == NULL);

   free(buf);
   fclose(fd);
}

TEST(fat32, read_content_of_shortname_file)
{
   char data[128] = {0};
   fat_header *hdr;
   fat_entry *e;
   size_t res;
   char *buf;
   FILE *fd;

   fd = fopen("build/fatpart", "rb");

   buf = (char*) calloc(1, BUFSIZE);
   res = fread(buf, 1, BUFSIZE, fd);
   ASSERT_GT(res, 0);

   hdr = (fat_header*)buf;
   e = fat_search_entry(hdr, fat_unknown, "/testdir/dir1/f1");
   ASSERT_TRUE(e != NULL);

   fat_read_whole_file(hdr, e, data, sizeof(data));

   ASSERT_STREQ("hello world!\n", data);

   free(buf);
   fclose(fd);
}

TEST(fat32, read_content_of_longname_file)
{
   char data[128] = {0};
   fat_header *hdr;
   fat_entry *e;
   size_t res;
   char *buf;
   FILE *fd;

   fd = fopen("build/fatpart", "rb");

   buf = (char*) calloc(1, BUFSIZE);
   res = fread(buf, 1, BUFSIZE, fd);
   ASSERT_GT(res, 0);

   hdr = (fat_header*)buf;
   e = fat_search_entry(hdr,
                        fat_unknown,
                        "/testdir/This_is_a_file_with_a_veeeery_long_name.txt");
   ASSERT_TRUE(e != NULL);
   fat_read_whole_file(hdr, e, data, sizeof(data));

   ASSERT_STREQ("Content of file with a long name\n", data);

   free(buf);
   fclose(fd);
}


TEST(fat32, crc_of_init)
{
   char *buf;
   FILE *fd = fopen("build/fatpart", "rb");

   buf = (char*) calloc(1, BUFSIZE);
   size_t res = fread(buf, 1, BUFSIZE, fd);
   ASSERT_GT(res, 0);

   fat_header *hdr = (fat_header*)buf;
   fat_entry *e = fat_search_entry(hdr, fat_unknown, "/sbin/init");

   char *content = (char*)calloc(1, e->DIR_FileSize);
   fat_read_whole_file(hdr, e, content, e->DIR_FileSize);

   uint32_t fat_crc = crc32(0, content, e->DIR_FileSize);

   free(content);
   fclose(fd);

   fd = fopen("build/init", "rb");

   res = fread(buf, 1, BUFSIZE, fd);
   ASSERT_EQ(e->DIR_FileSize, res);

   uint32_t actual_file_crc = crc32(0, buf, res);

   ASSERT_EQ(fat_crc, actual_file_crc);

   fclose(fd);
   free(buf);
}
