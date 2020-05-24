/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <dirent.h>

#include "devshell.h"
#include "sysenter.h"
#include "test_common.h"

int cmd_fatmm1(int argc, char **argv)
{
   int fd, rc;
   char *vaddr;
   char buf[512];
   size_t file_size;
   struct stat statbuf;
   const size_t page_size = getpagesize();
   const char *test_file_name = DEVSHELL_PATH;
   const size_t mmap_size = page_size * 2;
   const size_t mmap_off = page_size * 4;

   printf("Using '%s' as test file\n", test_file_name);
   fd = open(test_file_name, O_RDONLY, 0644);
   DEVSHELL_CMD_ASSERT(fd > 0);

   rc = fstat(fd, &statbuf);
   DEVSHELL_CMD_ASSERT(rc == 0);

   file_size = statbuf.st_size;
   printf("File size: %llu\n", (ull_t)file_size);

   vaddr = mmap(NULL,                   /* addr */
                mmap_size,              /* length */
                PROT_READ,              /* prot */
                MAP_SHARED,             /* flags */
                fd,                     /* fd */
                mmap_off);

   if (vaddr == (void *)-1) {
      fprintf(stderr, "ERROR: mmap failed: %s\n", strerror(errno));
      goto err_end;
   }

   printf("- Check that reading at `vaddr` succeeds\n");
   do_mm_read(vaddr);

   if (lseek(fd, mmap_off, SEEK_SET) < 0) {
      fprintf(stderr, "ERROR: lseek failed: %s\n", strerror(errno));
      goto err_end;
   }

   for (size_t t = 0; t < mmap_size; t += sizeof(buf)) {

      rc = read(fd, buf, sizeof(buf));
      DEVSHELL_CMD_ASSERT(rc == sizeof(buf));

      if (memcmp(buf, vaddr + t, sizeof(buf))) {
         fprintf(stderr, "ERROR: content does NOT match\n");
         goto err_end;
      }
   }

   printf("- Content matches!\n");

   printf("- Check we can fork() a process with fat mmaps\n");
   if (test_sig(do_mm_read, vaddr, 0, 0))
      goto err_end;

   printf("- Check that reading at `vaddr` still succeeds from parent\n");
   do_mm_read(vaddr);

   printf("- Now check for SIGSEGV in the unmapped areas\n");
   printf("- vaddr + mmap_off + mmap_size\n");

   if (test_sig(do_mm_read, vaddr + mmap_off + mmap_size, SIGSEGV, 0))
      goto err_end;

   printf("DONE\n");
   close(fd);
   return 0;

err_end:
   close(fd);
   return 1;
}
