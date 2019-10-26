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

   printf("[child] read from pipe...\n");

   rc = read(rfd, buf, sizeof(buf));

   if (rc < 0) {
      printf("[child] read returned %d, error: %s\n", rc, strerror(errno));
      exit(1);
   }

   buf[rc] = 0;

   printf("[child] got: '%s'\n", buf);
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

   printf("[parent] Creating the pipe..\n");

   {
      int pipefd[2];
      rc = pipe(pipefd);

      if (rc < 0) {
         printf("[parent] pipe() failed. Error: %s\n", strerror(errno));
         return 1;
      }

      rfd = pipefd[0];
      wfd = pipefd[1];
   }

   printf("[parent] Doing fork()..\n");
   childpid = fork();
   DEVSHELL_CMD_ASSERT(childpid >= 0);

   if (!childpid)
      pipe_cmd1_child(rfd, wfd);

   printf("[parent] Wait 100 ms\n");
   usleep(100 * 1000);

   printf("[parent] Writing 'hello' to the pipe..\n");

   strcpy(buf, "hello");
   rc = write(wfd, buf, strlen(buf));

   if (rc < 0) {
      printf("[parent] write() failed. Error: %s\n", strerror(errno));
      return 1;
   }

   printf("[parent] Done.\n");
   rc = waitpid(childpid, &wstatus, 0);

   if (rc < 0) {
      printf("[parent] waitpid() failed. Error: %s\n", strerror(errno));
      return 1;
   }

   printf("[parent] Child done.\n");
   return 0;
}

static void pipe_cmd2_child(int rfd, int wfd)
{
   char buf[64] = "whatever";
   int rc;

   printf("[child] close the read side of the pipe\n");
   close(rfd);

   printf("[child] write to pipe, expecting SIGPIPE\n");
   errno = 0;
   rc = write(wfd, buf, sizeof(buf));

   printf("[child] write returned %d (%s)\n", rc, strerror(errno));
   printf("[child] while we expected to die\n");
   exit(1);
}

/* Test the broken pipe case */
int cmd_pipe2(int argc, char **argv)
{
   char buf[64];
   int code, term_sig;
   int rfd, wfd;
   int wstatus;
   int rc;
   pid_t childpid;

   printf("[parent] Creating the pipe..\n");

   {
      int pipefd[2];
      rc = pipe(pipefd);

      if (rc < 0) {
         printf("[parent] pipe() failed. Error: %s\n", strerror(errno));
         return 1;
      }

      rfd = pipefd[0];
      wfd = pipefd[1];
   }

   printf("[parent] Close the read side of the pipe\n");
   close(rfd);

   printf("[parent] Doing fork()..\n");
   childpid = fork();
   DEVSHELL_CMD_ASSERT(childpid >= 0);

   if (!childpid)
      pipe_cmd2_child(rfd, wfd);

   printf("[parent] Done.\n");
   rc = waitpid(childpid, &wstatus, 0);

   if (rc < 0) {
      printf("[parent] waitpid() failed. Error: %s\n", strerror(errno));
      return 1;
   }

   printf("[parent] Child done.\n");

   code = WEXITSTATUS(wstatus);
   term_sig = WTERMSIG(wstatus);

   if (term_sig != SIGPIPE) {
      printf("[parent] The child didn't die with SIGPIPE as expected\n");
      printf("[parent] exit code: %d, term_sig: %d\n", code, term_sig);
      return 1;
   }

   printf("[parent] The child died with SIGPIPE, as expected\n");
   return 0;
}

static void pipe_cmd3_child(void)
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
   rc = test_sig(&pipe_cmd3_child, 0, 0);
   DEVSHELL_CMD_ASSERT(rc == 0);
}
