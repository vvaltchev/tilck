
#include <cstdio>
#include <gtest/gtest.h>

using namespace std;

extern "C" {
   void fat32_dump_info(void *fatpart_begin);
}

TEST(fat32, dumpinfo)
{
   char buf[1024];
   FILE *fd = fopen("build/fatpart", "rb");

   memset(buf, 0, 1024);
   fread(buf, 1024, 1, fd);

   for (int i = 0; i < 128; i++)
      printf("%x ", (int)(unsigned char)buf[i]);

   printf("\n");

   fclose(fd);
}
