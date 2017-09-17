
#include <cstdio>
#include <iostream>
#include <gtest/gtest.h>

using namespace std;

extern "C" {
   #include <fs/fat32.h>
   #include <utils.h>
}

#define BUFSIZE (16*1024*1024)

TEST(fat32, dumpinfo)
{
   char *buf;
   FILE *fd = fopen("build/fatpart", "rb");

   buf = (char*) calloc(1, BUFSIZE);
   size_t res = fread(buf, BUFSIZE, 1, fd);
   (void)res;

   fat_dump_info(buf);

   fat_header *hdr = (fat_header*)buf;
   fat_entry *e = fat_search_entry(hdr, "/testdir/dir1/f1");

   char data[128] = {0};
   fat_read_whole_file(hdr, e, data, 128);

   printf("data: '%s'\n", data);

   free(buf);
   fclose(fd);
}

TEST(fat32, crc_of_init)
{
   char *buf;
   FILE *fd = fopen("build/fatpart", "rb");

   buf = (char*) calloc(1, BUFSIZE);
   size_t res = fread(buf, BUFSIZE, 1, fd);
   (void)res;

   fat_header *hdr = (fat_header*)buf;
   fat_entry *e = fat_search_entry(hdr, "/sbin/init");

   char *content = (char*)calloc(1, e->DIR_FileSize);
   fat_read_whole_file(hdr, e, content, e->DIR_FileSize);

   printf("crc of init: %x\n", crc32(0, content, e->DIR_FileSize));

   free(content);
   free(buf);
   fclose(fd);
}
