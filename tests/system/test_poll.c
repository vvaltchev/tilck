/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <poll.h>

#include "devshell.h"

static void
regular_poll1_child(int rfd, int wfd)
{
   const static char msg[] = "hello from poll1!";
   int rc;
   char buf[64];

   printf("[child] Hello from child, wait 100ms\n");
   usleep(100 * 1000);

   printf("[child] write() on the pipe\n");
   rc = write(wfd, msg, sizeof(msg));

   if (rc <= 0) {
      printf("[child] write() returned %d -> %s\n", rc, strerror(errno));
      exit(1);
   }

   printf("[child] wait 200ms\n");
   usleep(200 * 1000);

   printf("[child] read from rfd\n");

   for (int i = 0; i < 64; i++) {

      rc = read(rfd, buf, sizeof(buf));

      if (rc <= 0) {
         printf("[child] read() returned %d -> %s\n", rc, strerror(errno));
         exit(1);
      }
   }

   exit(0);
}

/* Regular comunication with child via pipe, before poll timeout */
int cmd_poll1(int argc, char **argv)
{
   struct pollfd fds[1];
   int pipefd[2];
   int wstatus;
   int rc, fl, tot;
   int failed = 0;
   char buf[64];
   pid_t childpid;

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
      regular_poll1_child(pipefd[0], pipefd[1]);

   fds[0] = (struct pollfd) {
      .fd = pipefd[0],        /* read end of the pipe */
      .events = POLLIN
   };

   /* Now, wait for the child to start and write something to the pipe */

   do {
      rc = poll(fds, 1, 3000 /* ms */);
   } while (rc < 0 && errno == EINTR);

   if (rc < 0) {
      printf("[parent] ERROR: poll() failed with: %s\n", strerror(errno));
      failed = 1;
      goto wait_child_and_return;
   }

   if (rc == 0) {
      printf("[parent] ERROR: poll() timed out (unexpected)\n");
      failed = 1;
      goto wait_child_and_return;
   }

   if (rc != 1) {
      printf("[parent] ERROR: poll() returned %d (expected: 1)\n", rc);
      failed = 1;
      goto wait_child_and_return;
   }

   if (!(fds[0].revents & POLLIN)) {
      printf("[parent] ERROR: no POLLIN in fds[0].revents\n");
      failed = 1;
      goto wait_child_and_return;
   }

   rc = read(fds[0].fd, buf, sizeof(buf));

   if (rc < 0) {
      printf("[parent] ERROR: read(fds[0]) failed with: %s\n", strerror(errno));
      failed = 1;
      goto wait_child_and_return;
   }

   buf[rc] = 0;
   printf("[parent] Got: '%s' from child\n");
   printf("[parent] Now make wfd nonblock and fill the buffer\n");

   fl = fcntl(pipefd[1], F_GETFL, 0);

   if (fl < 0) {
      printf("[parent] fcntl(pipefd[1]) failed with: %s\n", strerror(errno));
      goto wait_child_and_return;
   }

   fl |= O_NONBLOCK;
   rc = fcntl(pipefd[1], F_SETFL, fl);
   DEVSHELL_CMD_ASSERT(rc == 0);

   tot = 0;

   do {

      if ((rc = write(pipefd[1], &buf[0], 1)) > 0)
         tot += rc;

   } while (rc > 0);

   printf("[parent] Restore the blocking mode on wfd\n");
   rc = fcntl(pipefd[1], F_SETFL, fl & ~O_NONBLOCK);
   DEVSHELL_CMD_ASSERT(rc == 0);

   printf("[parent] The pipe buffer is full after writing %d bytes\n", tot);
   printf("[parent] Now poll() wfd with POLLOUT\n");

   fds[0] = (struct pollfd) {
      .fd = pipefd[1],        /* write end of the pipe */
      .events = POLLOUT
   };

   do {
      rc = poll(fds, 1, 3000 /* ms */);
   } while (rc < 0 && errno == EINTR);

   if (rc < 0) {
      printf("[parent] ERROR: poll() failed with: %s\n", strerror(errno));
      failed = 1;
      goto wait_child_and_return;
   }

   if (rc == 0) {
      printf("[parent] ERROR: poll() timed out (unexpected)\n");
      failed = 1;
      goto wait_child_and_return;
   }

   if (rc != 1) {
      printf("[parent] ERROR: poll() returned %d (expected: 1)\n", rc);
      failed = 1;
      goto wait_child_and_return;
   }

   if (!(fds[0].revents & POLLOUT)) {
      printf("[parent] ERROR: no POLLOUT in fds[0].revents\n");
      failed = 1;
      goto wait_child_and_return;
   }

   printf("[parent] poll() completed with POLLOUT as expected\n");

wait_child_and_return:

   printf("[parent] waitpid()..\n");
   rc = waitpid(childpid, &wstatus, 0);

   if (rc < 0) {
      printf("[parent] waitpid() failed. Error: %s\n", strerror(errno));
      return 1;
   }

   printf("[parent] waitpid() done\n");
   close(pipefd[0]);
   close(pipefd[1]);
   return failed;
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

   if (rc != 0) {
      printf("poll() did *not* timeout as expected\n");
      return 1;
   }

   printf("poll() timed out, as expected\n");
   return 0;
}

int cmd_poll3(int argc, char **argv)
{
   struct pollfd fds[1];
   int pipefd[2];
   char buf[64];
   int rc;

   printf("Creating a pipe...\n");
   rc = pipe(pipefd);

   if (rc < 0) {
      printf("pipe() failed. Error: %s\n", strerror(errno));
      return 1;
   }

   fds[0] = (struct pollfd) {
      .fd = pipefd[0],
      .events = POLLIN
   };

   printf("Running poll(rfd, ..., 0 ms)\n");
   rc = poll(fds, ARRAY_SIZE(fds), 0);

   if (rc < 0) {
      perror("poll");
      return 1;
   }

   printf("poll() returned (immediately): %d\n", rc);

   if (rc != 0) {
      printf("poll() did return 0 as expected\n");
      return 1;
   }

   printf("Write something on the pipe\n");

   rc = write(pipefd[1], "hello", 5);
   DEVSHELL_CMD_ASSERT(rc == 5);

   printf("Running poll(rfd, ..., 0 ms)\n");
   rc = poll(fds, ARRAY_SIZE(fds), 0);

   if (rc < 0) {
      perror("poll");
      return 1;
   }

   printf("poll() returned (immediately): %d\n", rc);

   if (rc != 1) {
      printf("poll() did return 1 as expected\n");
      return 1;
   }

   printf("fds[0].revents = %p\n", fds[0].revents);

   if (!(fds[0].revents & POLLIN)) {
      printf("ERROR: fds[0].revents did *not* contain POLLIN, as expected\n");
      return 1;
   }

   printf("Everything is alright, got POLLIN\n");

   rc = read(fds[0].fd, buf, sizeof(buf));
   DEVSHELL_CMD_ASSERT(rc > 0);

   buf[rc] = 0;
   printf("Got from the pipe: '%s'\n", buf);

   close(pipefd[0]);
   close(pipefd[1]);
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
      return 1;
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
