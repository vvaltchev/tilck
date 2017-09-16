
#include <cstdio>
#include <gtest/gtest.h>

using namespace std;

extern "C" {
   void fat32_dump_info(void *fatpart_begin);
}

TEST(fat32, dumpinfo)
{
   char *buf;
   FILE *fd = fopen("build/fatpart", "rb");

   buf = (char*) calloc(1, 16*1024*1024);
   size_t res = fread(buf, 16*1024*1024, 1, fd);
   (void)res;

   fat32_dump_info(buf);
   free(buf);

   fclose(fd);
}
