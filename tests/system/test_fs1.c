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

static char pagebuf[4096];

static void create_test_file1(void)
{
   int fd, rc;

   fd = open("/tmp/test1", O_CREAT | O_WRONLY, 0644);
   DEVSHELL_CMD_ASSERT(fd > 0);

   printf("writing 'a'...\n");
   memset(pagebuf, 'a', 3 * KB);
   rc = write(fd, pagebuf, 3 * KB);
   DEVSHELL_CMD_ASSERT(rc == 3 * KB);

   printf("writing 'b'...\n");
   memset(pagebuf, 'b', 3 * KB);
   rc = write(fd, pagebuf, 3 * KB);
   DEVSHELL_CMD_ASSERT(rc == 3 * KB);

   printf("writing 'c'...\n");
   memset(pagebuf, 'c', 3 * KB);
   rc = write(fd, pagebuf, 3 * KB);
   DEVSHELL_CMD_ASSERT(rc == 3 * KB);

   close(fd);
}

static void write_on_test_file(void)
{
   int fd, rc;
   off_t off;
   char buf[32] = "hello world";

   fd = open("/tmp/test1", O_WRONLY);
   DEVSHELL_CMD_ASSERT(fd > 0);

   rc = write(fd, buf, 32);
   DEVSHELL_CMD_ASSERT(rc == 32);

   off = lseek(fd, 4096, SEEK_SET);
   DEVSHELL_CMD_ASSERT(off == 4096);

   rc = write(fd, "XXX", 3);
   DEVSHELL_CMD_ASSERT(rc == 3);

   close(fd);
}

static void read_past_end(void)
{
   int rc, fd;
   off_t off;
   char buf[32] = { [0 ... 30] = 'a', [31] = 0 };

   fd = open("/tmp/test1", O_RDONLY);
   DEVSHELL_CMD_ASSERT(fd > 0);

   off = lseek(fd, 64 * KB, SEEK_SET);
   printf("off: %d\n", (int)off);

   rc = read(fd, buf, sizeof(buf));
   DEVSHELL_CMD_ASSERT(rc == 0);
   printf("buf: '%s'\n", buf);
   close(fd);
}

/*
 * Generic create file/write/read/seek test
 */
int cmd_fs1(int argc, char **argv)
{
   create_test_file1();
   write_on_test_file();
   read_past_end();
   // TODO: add a function here to check how EXACTLY the file should look like
   unlink("/tmp/test1");
   return 0;
}

/*
 * Test creat() [indirectly O_TRUNC] and open(O_CREAT + O_EXCL).
 */
int cmd_fs2(int argc, char **argv)
{
   int fd, rc;
   char buf[32];
   struct stat statbuf;

   fd = creat("/tmp/test2", 0644);
   DEVSHELL_CMD_ASSERT(fd > 0);

   rc = write(fd, "test\n", 5);
   DEVSHELL_CMD_ASSERT(rc == 5);
   close(fd);

   /*
    * Being creat(path, mode) equivalent to:
    *    open(path, O_CREAT|O_WRONLY|O_TRUNC, mode)
    * we expect creat() to succeed even if the file already exists.
    */

   rc = creat("/tmp/test2", 0644);
   DEVSHELL_CMD_ASSERT(rc > 0);
   close(rc);

   /*
    * Now, since creat() implies O_TRUNC, we have to check that the file has
    * been actually truncated.
    */

   fd = open("/tmp/test2", O_RDONLY);
   DEVSHELL_CMD_ASSERT(fd > 0);
   rc = read(fd, buf, sizeof(buf));
   DEVSHELL_CMD_ASSERT(rc == 0);
   close(fd);

   rc = stat("/tmp/test2", &statbuf);
   DEVSHELL_CMD_ASSERT(rc == 0);
   DEVSHELL_CMD_ASSERT(statbuf.st_size == 0);
   DEVSHELL_CMD_ASSERT(statbuf.st_blocks == 0);

   /* Instead, this open() call using O_EXCL is expected to FAIL */
   rc = open("/tmp/test2", O_CREAT | O_EXCL | O_WRONLY, 0644);

   DEVSHELL_CMD_ASSERT(rc < 0);
   DEVSHELL_CMD_ASSERT(errno == EEXIST);

   unlink("/tmp/test2");
   return 0;
}

/*
 * Test a corner case for RAMFS: remove the next dentry while reading the
 * contents of a directory with getdents64().
 */
int cmd_fs3(int argc, char **argv)
{
   struct linux_dirent64 *de;
   char dentsbuf[192];
   int fd, rc, off = 0;
   DIR *d;

   rc = mkdir("/tmp/r", 0755);
   DEVSHELL_CMD_ASSERT(rc == 0);

   for (int i = 0; i < 20; i++)
      create_test_file("/tmp/r", i);

   d = opendir("/tmp/r");
   DEVSHELL_CMD_ASSERT(d != NULL);

   fd = dirfd(d);
   rc = getdents64(fd, (void *)dentsbuf, sizeof(dentsbuf));
   DEVSHELL_CMD_ASSERT(rc > 0);

   printf("getdents64: %d\n", rc);

   for (de = (void *) dentsbuf, off = 0; off < rc; off += de->d_reclen) {
      de = (void *)(dentsbuf + off);
      // printf("entry: '%s'\n", de->d_name);
   }

   int last_n = atoi(de->d_name + 5); /* skip "test_" */
   printf("last entry: '%s' (%d)\n", de->d_name, last_n);

   /*
    * The next elem, at least on Tilck's ramfs, will be "test_${last_n+1}"
    * because the getdents64 keep the creation order. Remove that elem.
    */

   printf("Remove the next entry: test_%03d\n", last_n + 1);
   remove_test_file("/tmp/r", last_n + 1);

   /*
    * Now, if this special case has been handled correctly, we should continue
    * from last_n + 2.
    */

   rc = getdents64(fd, (void *)dentsbuf, sizeof(dentsbuf));
   DEVSHELL_CMD_ASSERT(rc > 0);

   printf("getdents64: %d\n", rc);

   if (getenv("TILCK")) {

      /*
       * Tilck's RAMFS keep the creation order: that's why we can reason about
       * which dentry will the next one. In general, that's not possible.
       */

      de = (void *) dentsbuf;
      printf("Next entry: '%s'\n", de->d_name);
      DEVSHELL_CMD_ASSERT(atoi(de->d_name + 5) == last_n + 2);
      printf("The next dentry was last_n + 2 as expected\n");
   }

   rc = closedir(d);
   DEVSHELL_CMD_ASSERT(rc == 0);

   for (int i = 0; i < 20; i++) {

      if (i != last_n+1) {

         remove_test_file("/tmp/r", i);

      } else {

         char buf[32];
         sprintf(buf, "/tmp/r/%d", i);
         rc = unlink(buf);
         DEVSHELL_CMD_ASSERT(rc < 0);
         DEVSHELL_CMD_ASSERT(errno == ENOENT);
      }
   }

   rc = rmdir("/tmp/r");
   DEVSHELL_CMD_ASSERT(rc == 0);
   return 0;
}

static void
fs4_aux_read_dents(DIR *d,        /* dir handle */
                   long *dposs,   /* array of positions */
                   int s,         /* start index */
                   int n,         /* if >= 0, #elems, otherwise, all of them */
                   int j,         /* if >= 0, index of element to check name */
                   const char *v, /* if j >= 0, check name[j] == v */
                   bool cdots)    /* check that '.' and '..' exist */
{
   struct dirent *de;

   for (int i = s; n >= 0 ? (i < s + n) : 1; i++) {

      if (telldir(d) != dposs[i]) {
         printf("telldir != dposs for i = %d\n", i);
         printf("telldir: %d\n", telldir(d));
         printf("dposs:   %d\n", dposs[i]);
         exit(1);
      }

      de = readdir(d);

      if (n >= 0)
         DEVSHELL_CMD_ASSERT(de != NULL);
      else if (!de)
         break;

      // printf("dposs[%3d]: %11ld, entry: %s\n", i, dposs[i], de->d_name);

      if (cdots) {
         if (i == 0)
            DEVSHELL_CMD_ASSERT(strcmp(de->d_name, ".") == 0);

         if (i == 1)
            DEVSHELL_CMD_ASSERT(strcmp(de->d_name, "..") == 0);
      }

      if (j >= 0 && i == j)
         DEVSHELL_CMD_ASSERT(strcmp(de->d_name, v) == 0);
   }
}

static void
generic_fs_dir_seek_test(DIR *d,
                         const int n_files,
                         const int seek_n,
                         const int seek_n2,
                         bool cdots)
{
   char saved_entry_name[256];
   long dposs[n_files + 3];
   struct dirent *de;

   printf("Reading dir entries...\n");

   for (int i = 0; ; i++) {

      dposs[i] = telldir(d);
      de = readdir(d);

      if (!de) {
         printf("Done (%d dirents)\n", i);

         if (cdots)
            DEVSHELL_CMD_ASSERT(i >= n_files+2);
         else
            DEVSHELL_CMD_ASSERT(i >= n_files);

         break;
      }

      if (i == seek_n)
         strcpy(saved_entry_name, de->d_name);

      // printf("dposs[%3d]: %11ld, entry: %s\n", i, dposs[i], de->d_name);
   }

   printf("seek to the position of entry #%d: %ld\n", seek_n, dposs[seek_n]);
   seekdir(d, dposs[seek_n]);

   fs4_aux_read_dents(d, dposs, seek_n, -1, seek_n, saved_entry_name, cdots);

   printf("seek to the position of entry #%d: %ld\n", seek_n2, dposs[seek_n2]);
   seekdir(d, dposs[seek_n2]);

   fs4_aux_read_dents(d, dposs, seek_n2, 4, -1, NULL, cdots);

   seekdir(d, 0);
   printf("seek to the position of entry #0: 0\n");
   DEVSHELL_CMD_ASSERT(telldir(d) == 0);

   fs4_aux_read_dents(d, dposs, 0, 4, -1, NULL, cdots);

   printf("Do rewinddir()...\n");
   rewinddir(d);
   DEVSHELL_CMD_ASSERT(telldir(d) == 0);

   fs4_aux_read_dents(d, dposs, 0, 4, -1, NULL, cdots);
   printf("Everything looks good\n");
}

/*
 * Test telldir(), seekdir() and rewinddir() on RAMFS.
 */
int cmd_fs4(int argc, char **argv)
{
   const int n_files = 100;
   int rc;
   DIR *d;

   /* preparing the test environment */
   rc = mkdir("/tmp/r", 0755);
   DEVSHELL_CMD_ASSERT(rc == 0);

   for (int i = 0; i < n_files; i++)
      create_test_file("/tmp/r", i);

   d = opendir("/tmp/r");
   DEVSHELL_CMD_ASSERT(d != NULL);

   /* ---------------- actual test's code --------------------- */

   generic_fs_dir_seek_test(d, n_files, 90, 4, true);

   /* ---------------- clean up the test environment ----------- */
   rc = closedir(d);
   DEVSHELL_CMD_ASSERT(rc == 0);

   for (int i = 0; i < n_files; i++)
      remove_test_file("/tmp/r", i);

   rc = rmdir("/tmp/r");
   DEVSHELL_CMD_ASSERT(rc == 0);
   return 0;
}

/*
 * Test telldir(), seekdir() and rewinddir() on FAT32
 */
int cmd_fs5(int argc, char **argv)
{
   const int n_files = 100;
   DIR *d;
   int rc;

   if (!FAT_TEST_DIR) {
      printf("[devshell] Skip fs5 because FAT_TEST_DIR == 0\n");
      return 0;
   }

   d = opendir("/tdir");
   DEVSHELL_CMD_ASSERT(d != NULL);

   generic_fs_dir_seek_test(d, n_files, 90, 4, true);

   rc = closedir(d);
   DEVSHELL_CMD_ASSERT(rc == 0);
   return 0;
}

/*
 * Test telldir(), seekdir() and rewinddir() on DEVFS
 */
int cmd_fs6(int argc, char **argv)
{
   DIR *d;
   int rc;

   d = opendir("/dev");
   DEVSHELL_CMD_ASSERT(d != NULL);

   generic_fs_dir_seek_test(d, 6, 5, 2, false);

   rc = closedir(d);
   DEVSHELL_CMD_ASSERT(rc == 0);
   return 0;
}
