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

static void pipe1_child(int rfd, int wfd)
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
      pipe1_child(rfd, wfd);

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
