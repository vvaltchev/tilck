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

static void wait_and_close_child(int fd)
{
   printf("[child] Sleep 100 ms\n");
   usleep(100 * 1000);

   printf("[child] Close the other side of the pipe [fd: %d]\n", fd);
   close(fd);

   printf("[child] exit(0)\n");
   exit(0);
}

static int common_pollerr_pollhup_test(const bool close_read)
{
   struct pollfd fds[1];
   int pipefd[2];
   int wstatus;
   int rc;
   int saved_errno;
   pid_t childpid;
   int fail = 0;
   int fdn = close_read ? 0 : 1;
   const char *name = close_read ? "read" : "write";

   signal(SIGPIPE, SIG_IGN); /* ignore SIGPIPE */

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
      wait_and_close_child(pipefd[fdn]);

   printf("[parent] Close the %s side of the pipe [fd: %d]\n",
          name, pipefd[fdn]);

   close(pipefd[fdn]);

   fds[0] = (struct pollfd) {
      .fd = pipefd[1 - fdn],   /* read or write side of the pipe */
      .events = 0              /* wait for errors */
   };

   printf("[parent] poll({pipefd[%d]: %d, 0}, ...)\n", 1 - fdn, pipefd[1-fdn]);
   rc = poll(fds, 1, -1);

   if (rc < 0) {
      printf("[parent] poll() failed with: %s\n", strerror(errno));
      return 1;
   }

   printf("[parent] poll() returned: %d [expected: 0]\n", rc);

   if (fds[0].revents & POLLIN)
      printf("[parent] POLLIN in revents\n");

   if (fds[0].revents & POLLOUT)
      printf("[parent] POLLOUT in revents\n");

   if (fds[0].revents & POLLERR)
      printf("[parent] POLLERR in revents\n");

   if (fds[0].revents & POLLHUP)
      printf("[parent] POLLHUP in revents\n");

   if (close_read && !(fds[0].revents & POLLERR)) {
      printf("[parent] FAIL: No POLLERR in revents\n");
      fail = 1;
   }

   if (!close_read && !(fds[0].revents & POLLHUP)) {
      printf("[parent] FAIL: No POLLHUP in revents\n");
      fail = 1;
   }

   printf("[parent] waitpid()..\n");
   rc = waitpid(childpid, &wstatus, 0);

   if (rc < 0) {
      printf("[parent] waitpid() failed. Error: %s\n", strerror(errno));
      return 1;
   }

   printf("[parent] waitpid() done\n");
   printf("[parent] %s()...\n", name);
   errno = 0;

   if (close_read) {
      rc = write(pipefd[1], &wstatus, sizeof(wstatus));
   } else {
      rc = read(pipefd[0], &wstatus, sizeof(wstatus));
   }

   saved_errno = errno;

   if (rc > 0) {

      /* if rc > 0, fail in both cases */
      printf("[parent] FAIL: %s() unexpectedly returned %d!\n", name, rc);
      return 1;
   }

   printf("[parent] %s() returned %d; errno: %s\n",
          name, rc, saved_errno ? strerror(saved_errno) : "<no error>");

   if (close_read) {

      /*
       * When we close the read side of the pipe, we expect the write() to fail
       * with -1 and errno to be EPIPE.
       */

      if (!rc || saved_errno != EPIPE)
         fail = 1;

   } else {

      /*
       * When we close the write side of the pipe, we expect the read() to just
       * return 0.
       */

      if (rc < 0)
         fail = 1;
   }

   if (fail)
      printf("[parent] TEST *FAILED*\n");

   return fail;
}

/*
 * Use a pipe to communicate with child, expect POLLERR in parent after closing
 * the read side of the pipe.
 */

int cmd_pollerr(int argc, char **argv)
{
   return common_pollerr_pollhup_test(true);
}


/*
 * Use a pipe to communicate with child, expect POLLHUP in parent after closing
 * the write side of the pipe.
 */

int cmd_pollhup(int argc, char **argv)
{
   return common_pollerr_pollhup_test(false);
}
