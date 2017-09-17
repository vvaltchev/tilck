
#include <cstdio>
#include <gtest/gtest.h>

using namespace std;

extern "C" {
   #include <fs/fat32.h>
}

TEST(fat32, dumpinfo)
{
   char *buf;
   FILE *fd = fopen("build/fatpart", "rb");

   buf = (char*) calloc(1, 16*1024*1024);
   size_t res = fread(buf, 16*1024*1024, 1, fd);
   (void)res;

   fat_dump_info(buf);

   fat_entry *e = fat_search_entry((fat_header*)buf, "/TESTDIR/DIR1/F1");

   void *data = fat_get_pointer_to_first_cluster((fat_header*)buf, e);

   printf("buf addr: %p\n", buf);
   printf("data address: %p\n", data);
   printf("data offset: %p\n", (void*) ((u8*)data - (u8*)buf));

   char *str = (char*)data;
   str[8] = 0;
   printf("data[0..7] = '%s'\n", str);

   free(buf);

   fclose(fd);
}
