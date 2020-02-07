/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/fat32_base.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define MMAP_SIZE (40*1024*1024)

void show_help_and_exit(int argc, char **argv)
{
   printf("Syntax:\n");
   printf("    %s [--truncate] <fat part file>\n", argv[0]);
   exit(1);
}


int main(int argc, char **argv)
{
   int fd;
   void *vaddr;
   const char *file;
   bool trunc = false;
   u32 used_bytes, ff_clu_off;
   int failed = 0;

   if (argc < 2) {
      show_help_and_exit(argc, argv);
   }

   if (!strcmp(argv[1], "--truncate")) {

      if (argc < 3)
         show_help_and_exit(argc, argv);

      trunc = true;
      file = argv[2];

   } else {

      file = argv[1];
   }

   fd = open(file, O_RDWR);

   if (fd < 0) {
      perror("open() failed");
      return 1;
   }

   vaddr = mmap(NULL,                   /* addr */
                MMAP_SIZE,              /* length */
                PROT_READ|PROT_WRITE,   /* prot */
                MAP_SHARED,             /* flags */
                fd,                     /* fd */
                0);                     /* offset */

   if (vaddr == (void *)-1) {
      perror("mmap() failed");
      failed = 1;
      goto out;
   }

   /*
    * Don't do this here, forcing it to happen in the bootloaders.
    * fat_compact_clusters(vaddr);
    */

   ff_clu_off = fat_get_first_free_cluster_off(vaddr);
   used_bytes = fat_calculate_used_bytes(vaddr);

   if (ff_clu_off > used_bytes) {

      fprintf(stderr,
              "FATAL ERROR: ff_clu_off (%u) > used_bytes (%u)\n",
              ff_clu_off, used_bytes);

      failed = 1;
      goto out;
   }

   if (!trunc)
      printf("%u\n", used_bytes);

   if (munmap(vaddr, MMAP_SIZE) < 0) {
      perror("munmap() failed");
   }

   if (trunc) {
      if (ftruncate(fd, used_bytes) < 0) {
         perror("ftruncate() failed");
         failed = 1;
      }
   }

out:
   close(fd);
   return failed;
}
