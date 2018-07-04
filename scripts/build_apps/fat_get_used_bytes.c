
#include <exos/common/basic_defs.h>
#include <exos/common/fat32_base.h>

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

   errno = 0;

   vaddr = mmap(NULL,                   /* addr */
                MMAP_SIZE,              /* length */
                PROT_READ,              /* prot */
                MAP_SHARED,             /* flags */
                fd,                     /* fd */
                0);                     /* offset */

   if (errno) {
      perror("mmap() failed");
      close(fd);
      return 1;
   }

   u32 used_bytes = fat_get_used_bytes(vaddr);

   if (!trunc)
      printf("%u\n", used_bytes);

   if (munmap(vaddr, MMAP_SIZE) < 0) {
      perror("munmap() failed");
   }

   if (trunc) {
      ftruncate(fd, used_bytes);
   }

   close(fd);
   return 0;
}
