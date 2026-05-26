/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <3rd_party/crc32.h>

int read_wrapper(int fd, char *buf, int len)
{
   int rc;
   int tot = 0;

   while (tot < len) {

      rc = read(fd, buf + tot, len - tot);

      if (!rc)
         break;

      if (rc < 0)
         return rc;

      tot += rc;
   }

   return tot;
}

int main(int argc, char **argv)
{
   int fd, bs, rc;
   char *buf;
   int off = 0;

   if (argc < 3) {
      fprintf(stderr, "Usage: %s <file> <bufsize>\n", argv[0]);
      return 1;
   }

   bs = atoi(argv[2]);

   if (bs <= 0) {
      fprintf(stderr, "Invalid bufsize\n");
      return 1;
   }

   fd = open(argv[1], O_RDONLY);

   if (fd < 0) {
      perror("open");
      return 1;
   }

   buf = malloc(bs);

   if (!buf) {
      fprintf(stderr, "out-of-memory\n");
      return 1;
   }

   do {

      uint32_t checksum;

      if ((rc = read_wrapper(fd, buf, bs)) < 0) {
         perror("read");
         break;
      }

      checksum = crc32(0, buf, rc);
      printf("[%08x] %08x\n", off, checksum);

      off += rc;

   } while (rc == bs);

   free(buf);
   close(fd);
   return 0;
}
