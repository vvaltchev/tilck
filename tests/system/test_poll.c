/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <poll.h>

#include "devshell.h"

int cmd_poll1(int argc, char **argv)
{
   int rc, cnt = 0;
   struct pollfd fds[] = {
      { .fd = 0, .events = POLLIN }
   };

   printf("Running poll(fd, ..., 3000 ms)\n");
   rc = poll(fds, ARRAY_SIZE(fds), 3000);

   if (rc < 0) {
      perror("poll");
      return 1;
   }

   printf("poll() returned: %d\n", rc);

   if (!rc)
      printf("poll() timed out\n");

   for (u32 i = 0; i < ARRAY_SIZE(fds); i++) {
      if (fds[i].revents & POLLIN) {
         printf("fd %d -> POLLIN\n", fds[i].fd);
         cnt++;
      }
   }

   return 0;
}

int cmd_poll2(int argc, char **argv)
{
   int rc, cnt = 0;
   struct pollfd fds[] = {
      { .fd = 0, .events = POLLIN }
   };

   /* NOTE: using timeout of 1 ms which is < 1 tick */
   printf("Running poll(fd, ..., 1 ms)\n");
   rc = poll(fds, ARRAY_SIZE(fds), 1);

   if (rc < 0) {
      perror("poll");
      return 1;
   }

   printf("poll() returned: %d\n", rc);

   if (!rc)
      printf("poll() timed out\n");

   for (u32 i = 0; i < ARRAY_SIZE(fds); i++) {
      if (fds[i].revents & POLLIN) {
         printf("fd %d -> POLLIN\n", fds[i].fd);
         cnt++;
      }
   }

   return 0;
}

int cmd_poll3(int argc, char **argv)
{
   int rc, cnt = 0;
   struct pollfd fds[] = {
      { .fd = 0, .events = POLLIN }
   };

   printf("Running poll(fd, ..., 0 ms)\n");
   rc = poll(fds, ARRAY_SIZE(fds), 0);

   if (rc < 0) {
      perror("poll");
      return 1;
   }

   printf("poll() returned (immediately): %d\n", rc);

   for (u32 i = 0; i < ARRAY_SIZE(fds); i++) {
      if (fds[i].revents & POLLIN) {
         printf("fd %d -> POLLIN\n", fds[i].fd);
         cnt++;
      }
   }

   return 0;
}

static void cmd_pollerr_child(int rfd)
{
   printf("[child] Sleep 100 ms\n");
   usleep(100 * 1000);

   printf("[child] Close the read side of the pipe [fd: %d]\n", rfd);
   close(rfd);

   printf("[child] exit(0)\n");
   exit(0);
}

/*
 * Use a pipe to communicate with child, expect POLLERR in parent after closing
 * the read side of the pipe.
 */

int cmd_pollerr(int argc, char **argv)
{
   struct pollfd fds[1];
   int pipefd[2];
   int wstatus;
   int rc;
   int saved_errno;
   pid_t childpid;
   int fail = 0;

   printf("[parent] Calling pipe()\n");
   rc = pipe(pipefd);

   if (rc < 0) {
      printf("[parent] pipe() failed. Error: %s\n", strerror(errno));
      exit(1);
   }

   printf("[parent] fork()..\n");
   childpid = fork();
   DEVSHELL_CMD_ASSERT(childpid >= 0);

   if (!childpid)
      cmd_pollerr_child(pipefd[0]);

   printf("[parent] Close the read side of the pipe [fd: %d]\n", pipefd[0]);
   close(pipefd[0]);

   fds[0] = (struct pollfd) {
      .fd = pipefd[1],   /* write side of the pipe */
      .events = 0        /* wait for errors */
   };

   printf("[parent] poll({pipefd[1], POLLOUT}, ...)\n");
   rc = poll(fds, 1, -1);

   if (rc < 0) {
      printf("[parent] poll() failed with: %s\n", strerror(errno));
      return 1;
   }

   printf("[parent] poll() returned: %d [expected: 0]\n", rc);

   if (fds[0].revents & POLLOUT)
      printf("[parent] POLLOUT in revents\n");

   if (fds[0].revents & POLLERR) {
      printf("[parent] POLLERR in revents\n");
   } else {
      printf("[parent] FAIL: No POLLERR in revents\n");
      fail = 1;
   }

   printf("[parent] waitpid()..\n");
   rc = waitpid(childpid, &wstatus, 0);

   if (rc < 0) {
      printf("[parent] waitpid() failed. Error: %s\n", strerror(errno));
      return 1;
   }

   printf("[parent] waitpid() done\n");


   signal(SIGPIPE, SIG_IGN); /* ignore SIGPIPE */
   printf("[parent] write()...\n");

   rc = write(pipefd[1], &rc, sizeof(rc));
   saved_errno = errno;

   if (rc >= 0) {
      printf("[parent] write() did NOT fail as expected. Ret: %d\n", rc);
      fail = 1;
   } else {
      printf("[parent] write() ret %d, err: %s\n", rc, strerror(saved_errno));
      DEVSHELL_CMD_ASSERT(saved_errno == EPIPE);
   }

   return fail;
}
