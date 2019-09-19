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

void create_test_file1(void);
void write_on_test_file1(void);

/* Test truncate() */
int cmd_fs7(int argc, char **argv)
{
   int rc;

   create_test_file1();
   write_on_test_file1();
   rc = truncate("/tmp/test1", 157);
   DEVSHELL_CMD_ASSERT(rc == 0);

   // TODO: add checks here to verify that the truncation worked as expected

   rc = unlink("/tmp/test1");
   DEVSHELL_CMD_ASSERT(rc == 0);
   return 0;
}

/* mmap file */
int cmd_fmmap1(int argc, char **argv)
{
   static const char test_str[] = "this is a test string\n";
   static const char test_str_exp[] = "This is a test string\n";
   static const char test_file[] = "/tmp/test1";

   int fd, rc;
   char *vaddr;
   char buf[64];
   struct stat statbuf;
   size_t file_size;
   const size_t page_size = getpagesize();

   printf("Using '%s' as test file\n", test_file);
   fd = open(test_file, O_CREAT | O_RDWR, 0644);
   DEVSHELL_CMD_ASSERT(fd > 0);

   rc = write(fd, test_str, sizeof(test_str)-1);
   DEVSHELL_CMD_ASSERT(rc == sizeof(test_str)-1);

   rc = fstat(fd, &statbuf);
   DEVSHELL_CMD_ASSERT(rc == 0);

   file_size = statbuf.st_size;
   printf("File size: %llu\n", (ull_t)file_size);

   vaddr = mmap(NULL,                   /* addr */
                2 * page_size,          /* length */
                PROT_READ | PROT_WRITE, /* prot */
                MAP_SHARED,             /* flags */
                fd,                     /* fd */
                0);

   if (vaddr == (void *)-1) {
      fprintf(stderr, "mmap failed: %s\n", strerror(errno));
      close(fd);
      unlink(test_file);
      DEVSHELL_CMD_ASSERT(vaddr != (void *)-1);
   }

   printf("vaddr: %p\n", vaddr);

   vaddr[0] = 'T';                     // has real effect
   vaddr[file_size] = '?';             // gets ignored as past of EOF
   vaddr[file_size + 10] = 'x';        // gets ignored as past of EOF
   vaddr[file_size + 11] = '\n';       // gets ignored as past of EOF

   // vaddr[page_size] = 'y';          // triggers SIGBUS as past of EOF and
                                       // in a new page. Requires a separate
                                       // test.

   close(fd);

   rc = stat(test_file, &statbuf);
   DEVSHELL_CMD_ASSERT(rc == 0);
   DEVSHELL_CMD_ASSERT(statbuf.st_size == file_size);

   fd = open(test_file, O_CREAT | O_RDONLY, 0644);
   DEVSHELL_CMD_ASSERT(fd > 0);

   rc = read(fd, buf, file_size);
   DEVSHELL_CMD_ASSERT(rc == file_size);
   buf[rc] = 0;

   if (strcmp(buf, test_str_exp)) {
      fprintf(stderr, "File contents != expected:\n");
      fprintf(stderr, "Contents: <<\n%s>>\n", buf);
      fprintf(stderr, "Expected: <<\n%s>>\n", test_str_exp);
      DEVSHELL_CMD_ASSERT(false);
   }

   close(fd);
   rc = unlink(test_file);
   DEVSHELL_CMD_ASSERT(rc == 0);
}
