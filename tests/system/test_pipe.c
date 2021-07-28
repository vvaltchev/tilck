/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "devshell.h"
#include "test_common.h"

static void pipe_cmd1_child(int rfd, int wfd)
{
   char buf[64];
   int rc;

   printf(STR_CHILD "read from pipe...\n");

   rc = read(rfd, buf, sizeof(buf));

   if (rc < 0) {
      printf(STR_CHILD "read returned %d, error: %s\n", rc, strerror(errno));
      exit(1);
   }

   buf[rc] = 0;

   printf(STR_CHILD "got: '%s'\n", buf);
   printf(STR_CHILD "Done.\n");
   exit(0);
}

/* Basic pipe read/write test between parent and child process */
int cmd_pipe1(int argc, char **argv)
{
   char buf[64];
   int rfd, wfd;
   int wstatus;
   int rc;
   pid_t childpid;

   printf(STR_PARENT "Creating the pipe..\n");

   {
      int pipefd[2];
      rc = pipe(pipefd);

      if (rc < 0) {
         printf(STR_PARENT "pipe() failed. Error: %s\n", strerror(errno));
         return 1;
      }

      rfd = pipefd[0];
      wfd = pipefd[1];
   }

   printf(STR_PARENT "Doing fork()..\n");
   childpid = fork();
   DEVSHELL_CMD_ASSERT(childpid >= 0);

   if (!childpid)
      pipe_cmd1_child(rfd, wfd);

   printf(STR_PARENT "Wait 100 ms\n");
   usleep(100 * 1000);

   printf(STR_PARENT "Writing 'hello' to the pipe..\n");

   strcpy(buf, "hello");
   rc = write(wfd, buf, strlen(buf));

   if (rc < 0) {
      printf(STR_PARENT "write() failed. Error: %s\n", strerror(errno));
      return 1;
   }

   printf(STR_PARENT "Done.\n");
   rc = waitpid(childpid, &wstatus, 0);

   if (rc < 0) {
      printf(STR_PARENT "waitpid() failed. Error: %s\n", strerror(errno));
      return 1;
   }

   printf(STR_PARENT "Child done.\n");
   return 0;
}

static void pipe_cmd2_child(int rfd, int wfd)
{
   char buf[64] = "whatever";
   int rc;

   printf(STR_CHILD "close the read side of the pipe\n");
   close(rfd);

   printf(STR_CHILD "write to pipe, expecting SIGPIPE\n");
   errno = 0;
   rc = write(wfd, buf, sizeof(buf));

   printf(STR_CHILD "write returned %d (%s)\n", rc, strerror(errno));
   printf(STR_CHILD "while we expected to die\n");
   exit(1);
}

/* Test the broken pipe case */
int cmd_pipe2(int argc, char **argv)
{
   int code, term_sig;
   int rfd, wfd;
   int wstatus;
   int rc;
   pid_t childpid;

   printf(STR_PARENT "Creating the pipe..\n");

   {
      int pipefd[2];
      rc = pipe(pipefd);

      if (rc < 0) {
         printf(STR_PARENT "pipe() failed. Error: %s\n", strerror(errno));
         return 1;
      }

      rfd = pipefd[0];
      wfd = pipefd[1];
   }

   printf(STR_PARENT "Close the read side of the pipe\n");
   close(rfd);

   printf(STR_PARENT "Doing fork()..\n");
   childpid = fork();
   DEVSHELL_CMD_ASSERT(childpid >= 0);

   if (!childpid)
      pipe_cmd2_child(rfd, wfd);

   printf(STR_PARENT "Done.\n");
   rc = waitpid(childpid, &wstatus, 0);

   if (rc < 0) {
      printf(STR_PARENT "waitpid() failed. Error: %s\n", strerror(errno));
      return 1;
   }

   printf(STR_PARENT "Child done.\n");

   code = WEXITSTATUS(wstatus);
   term_sig = WTERMSIG(wstatus);

   if (term_sig != SIGPIPE) {
      printf(STR_PARENT "The child didn't die with SIGPIPE as expected\n");
      printf(STR_PARENT "exit code: %d, term_sig: %d\n", code, term_sig);
      return 1;
   }

   printf(STR_PARENT "The child died with SIGPIPE, as expected\n");
   return 0;
}

static void pipe_cmd3_child(void *unused_arg)
{
   int pipefd[2];
   int rc;

   printf("Ignore SIGPIPE..\n");
   signal(SIGPIPE, SIG_IGN); /* ignore SIGPIPE */

   printf("Calling pipe()\n");
   rc = pipe(pipefd);

   if (rc < 0) {
      printf("pipe() failed. Error: %s\n", strerror(errno));
      exit(1);
   }

   printf("Close the read side of the pipe...\n");
   close(pipefd[0]);

   printf("Trying to write something..\n");
   rc = write(pipefd[1], &rc, sizeof(rc));

   if (rc >= 0) {
      printf("write() succeeded returning %d instead of failing\n", rc);
      exit(1);
   }

   if (errno != EPIPE) {
      printf("write() failed with %s instead of with EPIPE\n", strerror(errno));
      exit(1);
   }

   printf("write() failed with EPIPE, as expected\n");
   exit(0);
}

/* Test the broken pipe case with SIGPIPE ignored */
int cmd_pipe3(int argc, char **argv)
{
   int rc;

   printf("Try the broken pipe case with SIGPIPE ignored...\n");
   rc = test_sig(&pipe_cmd3_child, NULL, 0, 0, 0);
   DEVSHELL_CMD_ASSERT(rc == 0);
   return 0;
}

/* Test pipes with O_NONBLOCK */
int cmd_pipe4(int argc, char **argv)
{
   char buf[64];
   int pipefd[2];
   int rc;
   int tot;

   printf("Calling pipe()\n");
   rc = pipe(pipefd);

   if (rc < 0) {
      printf("pipe() failed. Error: %s\n", strerror(errno));
      return 1;
   }

   printf("Set the read fd in NONBLOCK mode\n");
   rc = fcntl(pipefd[0], F_GETFL);

   if (rc < 0) {
      printf("fcntl(pipefd[0], F_GETFL) failed: %s\n", strerror(errno));
      return 1;
   }

   rc = fcntl(pipefd[0], F_SETFL, rc | O_NONBLOCK);

   if (rc < 0) {
      printf("fcntl(pipefd[0], F_SETFL) failed: %s\n", strerror(errno));
      return 1;
   }

   printf("read() expecting to get EAGAIN\n");

   rc = read(pipefd[0], buf, sizeof(buf));
   DEVSHELL_CMD_ASSERT(rc == -1);
   DEVSHELL_CMD_ASSERT(errno == EAGAIN);

   printf("write() hello\n");
   rc = write(pipefd[1], "hello", 5);
   DEVSHELL_CMD_ASSERT(rc == 5);

   printf("read() expecting to get 'hello'\n");
   rc = read(pipefd[0], buf, sizeof(buf));
   DEVSHELL_CMD_ASSERT(rc == 5);
   buf[rc] = 0;

   DEVSHELL_CMD_ASSERT(!strcmp(buf, "hello"));

   printf("Set the write fd in NONBLOCK mode\n");

   rc = fcntl(pipefd[1], F_GETFL);

   if (rc < 0) {
      printf("fcntl(pipefd[0], F_GETFL) failed: %s\n", strerror(errno));
      return 1;
   }

   rc = fcntl(pipefd[1], F_SETFL, rc | O_NONBLOCK);

   if (rc < 0) {
      printf("fcntl(pipefd[0], F_SETFL) failed: %s\n", strerror(errno));
      return 1;
   }

   printf("write until EAGAIN is hit\n");

   for (tot = 0; ; tot++) {

      rc = write(pipefd[1], buf, 1);

      if (rc < 0)
         break;
   }

   rc = errno;

   printf("write() failed [expected] with: %s\n", strerror(rc));
   printf("bytes written: %d\n", tot);

   DEVSHELL_CMD_ASSERT(rc == EAGAIN);

   printf("Done.\n");
   close(pipefd[0]);
   close(pipefd[1]);
   return 0;
}

static void
pipe_reader_child(int id, int fd)
{
   int tot_read = 0;
   char buf[4096];
   int rc;
   int fail = 0;
   int to_read;

   while (true) {

      to_read = rand() % sizeof(buf) + 1;
      rc = read(fd, buf, to_read);

      if (rc < 0) {

         printf("[reader %d]: read(%d) failed with %s\n",
                id, to_read, strerror(errno));

         fail = 1;
         break;
      }

      if (rc == 0)
         break;

      tot_read += rc;
   }

   printf("[reader %d]: tot_read: %d (%d KB)\n", id, tot_read, tot_read / KB);
   exit(fail);
}

static void
pipe_writer_child(int id, int fd, int write_lim)
{
   int tot_written = 0;
   char buf[4096];
   int rc;
   int fail = 0;
   int to_write = 0;

   while (tot_written < write_lim) {

      to_write = rand() % sizeof(buf) + 1;

      if (tot_written + to_write > write_lim) {
         /* Make sure to write no more than `write_lim` */
         to_write = write_lim - tot_written;
      }

      rc = write(fd, buf, to_write);

      if (rc < 0) {

         printf("[writer %d]: write(%d) failed with %s\n",
                id, to_write, strerror(errno));

         fail = 1;
         break;
      }

      tot_written += rc;
   }

   printf("[writer %d]: tot_written: %d (%d KB)\n",
          id, tot_written, tot_written / KB);

   close(fd);
   exit(fail);
}

static int
pipe_random_test(int readers, int writers)
{
   int pipefd[2];
   int wstatus;
   int rc;
   int fail = 0;

   printf("[Pipe random test] readers: %d, writers: %d\n", readers, writers);

   rc = pipe(pipefd);

   if (rc < 0) {
      printf("pipe() failed. Error: %s\n", strerror(errno));
      return 1;
   }

   if (!getenv("TILCK")) {
      fcntl(pipefd[0], F_SETPIPE_SZ, 4096);
      fcntl(pipefd[1], F_SETPIPE_SZ, 4096);
   }

   for (int i = 0; i < writers; i++) {

      rc = fork();

      if (rc < 0) {
         printf("[Pipe random test] fork failed with %s\n", strerror(errno));
         fail = 1;
         break;
      }

      if (!rc) {
         close(pipefd[0]);
         pipe_writer_child(i, pipefd[1], readers * 32 * KB);
      }
   }

   for (int i = 0; i < readers; i++) {

      rc = fork();

      if (rc < 0) {
         printf("[Pipe random test] fork failed with %s\n", strerror(errno));
         fail = 1;
         break;
      }

      if (!rc) {
         close(pipefd[1]);
         pipe_reader_child(i, pipefd[0]);
      }
   }

   close(pipefd[0]);
   close(pipefd[1]);
   printf("[Pipe random test] wait for children\n");

   do {

      rc = waitpid(-1, &wstatus, 0);

      if (rc > 0) {


         int code = WEXITSTATUS(wstatus);
         int term_sig = WTERMSIG(wstatus);

         if (code || term_sig) {

            printf("[Pipe random test] child %d failed, "
                   "code: %d, sig: %d\n", rc, code, term_sig);

            fail = 1;
         }
      }

   } while (rc > 0);

   printf("[Pipe random test] done\n");
   return fail;
}

int cmd_pipe5(int argc, char **argv)
{
   int rc;

   if ((rc = pipe_random_test(10, 1)))
      return rc;

   if ((rc = pipe_random_test(1, 10)))
      return rc;

   if ((rc = pipe_random_test(5, 5)))
      return rc;

   return 0;
}
