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

static const char test_str[] = "this is a test string\n";
static const char test_str2[] = "hello from the 2nd page";
static const char test_str_exp[] = "This is a test string\n";
static const char test_file[] = "/tmp/test1";

/* mmap file */
int cmd_fmmap1(int argc, char **argv)
{
   int fd, rc;
   char *vaddr;
   char buf[64];
   size_t file_size;
   struct stat statbuf;
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

   if (vaddr == (void *)-1)
      goto err_case;

   printf("vaddr: %p\n", vaddr);

   vaddr[0] = 'T';                     // has real effect
   vaddr[file_size +  0] = '?';        // gets ignored as past of EOF
   vaddr[file_size + 10] = 'x';        // gets ignored as past of EOF
   vaddr[file_size + 11] = '\n';       // gets ignored as past of EOF

   // vaddr[page_size] = 'y';          // triggers SIGBUS as past of EOF and
                                       // in a new page. Requires a separate
                                       // test.

   close(fd);

   rc = stat(test_file, &statbuf);
   DEVSHELL_CMD_ASSERT(rc == 0);
   DEVSHELL_CMD_ASSERT(statbuf.st_size == file_size);

   fd = open(test_file, O_RDONLY, 0644);
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

   /* Now, let's mmap the file again and check the past-EOF contents */

   vaddr = mmap(NULL,                   /* addr */
                2 * page_size,          /* length */
                PROT_READ,              /* prot */
                MAP_SHARED,             /* flags */
                fd,                     /* fd */
                0);

   if (vaddr == (void *)-1)
      goto err_case;

   /*
    * At least on ext4 on Linux, the past-EOF contents are kept. That's the
    * simplest behavior to implement for Tilck as well.
    */
   printf("vaddr[file_size +  0]: %c\n", vaddr[file_size +  0]);
   printf("vaddr[file_size + 10]: %c\n", vaddr[file_size + 10]);

   DEVSHELL_CMD_ASSERT(vaddr[file_size +  0] == '?');
   DEVSHELL_CMD_ASSERT(vaddr[file_size + 10] == 'x');

   close(fd);
   rc = unlink(test_file);
   DEVSHELL_CMD_ASSERT(rc == 0);
   return 0;

err_case:
   fprintf(stderr, "mmap failed: %s\n", strerror(errno));
   close(fd);
   unlink(test_file);
   DEVSHELL_CMD_ASSERT(vaddr != (void *)-1);
}

/* mmap file and then do a partial unmap */
static void fmmap2_read_unmapped_mem(void)
{
   int fd, rc;
   char *vaddr;
   size_t file_size;
   char buf[64] = {0};
   char *page_size_buf;
   const size_t page_size = getpagesize();

   printf("Using '%s' as test file\n", test_file);
   fd = open(test_file, O_CREAT | O_RDWR, 0644);
   DEVSHELL_CMD_ASSERT(fd > 0);

   page_size_buf = malloc(page_size);
   DEVSHELL_CMD_ASSERT(page_size_buf != NULL);

   for (int i = 0; i < 4; i++) {
      memset(page_size_buf, 'A'+i, page_size);
      rc = write(fd, page_size_buf, page_size);
      DEVSHELL_CMD_ASSERT(rc == page_size);
   }

   /* Now, let's mmap the file */

   vaddr = mmap(NULL,                   /* addr */
                4 * page_size,          /* length */
                PROT_READ,              /* prot */
                MAP_SHARED,             /* flags */
                fd,                     /* fd */
                0);

   DEVSHELL_CMD_ASSERT(vaddr != (void *)-1);

   /* Un-map the 2nd page */
   rc = munmap(vaddr + page_size, page_size);
   DEVSHELL_CMD_ASSERT(rc == 0);

   /* Excepting to receive SIGSEGV from the kernel here */
   memcpy(buf, vaddr + page_size, sizeof(buf) - 1);

   /* ----------- We should NOT get here ------------------- */

   free(page_size_buf);
   close(fd);
   rc = unlink(test_file);
   DEVSHELL_CMD_ASSERT(rc == 0);
   exit(1);
}

/* mmap file and then do a partial unmap */
int cmd_fmmap2(int argc, char **argv)
{
   int rc = test_sig(fmmap2_read_unmapped_mem, SIGSEGV, 0);
   unlink(test_file);
   return rc;
}

/* mmap file with offset > 0 */
int cmd_fmmap3(int argc, char **argv)
{
   int fd, rc;
   char *vaddr;
   size_t file_size;
   char buf[64] = {0};
   char exp_buf[64] = {0};
   char *page_size_buf;
   const size_t page_size = getpagesize();
   bool failed = false;

   printf("Using '%s' as test file\n", test_file);
   fd = open(test_file, O_CREAT | O_RDWR, 0644);
   DEVSHELL_CMD_ASSERT(fd > 0);

   page_size_buf = malloc(page_size);
   DEVSHELL_CMD_ASSERT(page_size_buf != NULL);

   for (int i = 0; i < 4; i++) {
      memset(page_size_buf, 'A'+i, page_size);
      rc = write(fd, page_size_buf, page_size);
      DEVSHELL_CMD_ASSERT(rc == page_size);
   }

   /* Now, let's mmap the file at offset > 0 */

   vaddr = mmap(NULL,                   /* addr */
                4 * page_size,          /* length */
                PROT_READ,              /* prot */
                MAP_SHARED,             /* flags */
                fd,                     /* fd */
                page_size);             /* offset */

   DEVSHELL_CMD_ASSERT(vaddr != (void *)-1);

   memcpy(buf, vaddr, sizeof(buf) - 1);
   memset(exp_buf, 'B', sizeof(exp_buf)-1);

   if (strcmp(buf, exp_buf)) {
      fprintf(stderr, "Reading vaddr mapped at off > 0 lead to garbage.\n");
      fprintf(stderr, "Expected: '%s'\n", exp_buf);
      fprintf(stderr, "Got     : '%s'\n", buf);
      failed = true;
   }

   free(page_size_buf);
   close(fd);
   rc = unlink(test_file);
   DEVSHELL_CMD_ASSERT(rc == 0);
   return failed;
}

static void fmmap4_read_write_after_eof(bool rw)
{
   int fd, rc;
   char *vaddr;
   size_t file_size;
   char buf[64] = {0};
   struct stat statbuf;
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

   DEVSHELL_CMD_ASSERT(vaddr != (void *)-1);

   /* Expecting to be killed by SIGBUS here */

   if (!rw) {
      /* Read past EOF */
      memcpy(buf, vaddr + page_size, sizeof(buf) - 1);
   } else {
      /* Write past EOF */
      memcpy(vaddr + page_size, buf, sizeof(buf) - 1);
   }

   /* If we got here, something went wrong */
}

static void fmmap4_read_after_eof(void)
{
   fmmap4_read_write_after_eof(false);
}

static void fmmap4_write_after_eof(void)
{
   fmmap4_read_write_after_eof(true);
}

/* mmap file and read past EOF, expecting SIGBUS */
int cmd_fmmap4(int argc, char **argv)
{
   int rc;

   if ((rc = test_sig(fmmap4_read_after_eof, SIGBUS, 0)))
      goto end;

   if ((rc = test_sig(fmmap4_write_after_eof, SIGBUS, 0)))
      goto end;

end:
   unlink(test_file);
   return rc;
}

/* mmap file, truncate (expand) it and write past the original EOF */
int cmd_fmmap5(int argc, char **argv)
{
   int fd, rc;
   char *vaddr;
   size_t file_size;
   char buf[64] = {0};
   struct stat statbuf;
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

   DEVSHELL_CMD_ASSERT(vaddr != (void *)-1);

   rc = ftruncate(fd, page_size + 128);
   DEVSHELL_CMD_ASSERT(rc == 0);

   rc = fstat(fd, &statbuf);
   DEVSHELL_CMD_ASSERT(rc == 0);

   file_size = statbuf.st_size;
   printf("(NEW) File size: %llu\n", (ull_t)file_size);

   /*
    * This memory write will trigger a page-fault and the kernel should allocate
    * on-the-fly the page (ramfs_block) for us and, ultimately, resume the
    * write.
    */
   strcpy(vaddr + page_size, test_str2);

   close(fd);
   rc = unlink(test_file);
   DEVSHELL_CMD_ASSERT(rc == 0);
   return 0;
}
