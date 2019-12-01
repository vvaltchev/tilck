/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/select.h>

#include "devshell.h"

void regular_poll_or_select_on_pipe_child(int rfd, int wfd);


/* Regular comunication with child via pipe, before poll timeout */
int cmd_select1(int argc, char **argv)
{
   struct timeval tv;
   fd_set readfds, writefds;
   int nfds, pipefd[2];
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
      regular_poll_or_select_on_pipe_child(pipefd[0], pipefd[1]);

   /* Wait on the read end of the pipe */
   FD_ZERO(&readfds);
   FD_ZERO(&writefds);
   FD_SET(pipefd[0], &readfds);
   tv.tv_sec = 3;
   tv.tv_usec = 0;
   nfds = pipefd[0] + 1;

   /* Now, wait for the child to start and write something to the pipe */

   do {
      rc = select(nfds, &readfds, &writefds, NULL, &tv);
   } while (rc < 0 && errno == EINTR);

   if (rc < 0) {
      printf("[parent] ERROR: select() failed with: %s\n", strerror(errno));
      failed = 1;
      goto wait_child_and_return;
   }

   if (rc == 0) {
      printf("[parent] ERROR: select() timed out (unexpected)\n");
      failed = 1;
      goto wait_child_and_return;
   }

   if (rc != 1) {
      printf("[parent] ERROR: select() returned %d (expected: 1)\n", rc);
      failed = 1;
      goto wait_child_and_return;
   }

   if (!FD_ISSET(pipefd[0], &readfds)) {
      printf("[parent] ERROR: pipefd[0] is NOT set in readfds\n");
      failed = 1;
      goto wait_child_and_return;
   }

   rc = read(pipefd[0], buf, sizeof(buf));

   if (rc < 0) {
      printf("[parent] ERROR: read(rfd]) failed with: %s\n", strerror(errno));
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
   printf("[parent] Now select() wfd with writefds\n");

   FD_ZERO(&readfds);
   FD_ZERO(&writefds);
   FD_SET(pipefd[1], &writefds);
   tv.tv_sec = 3;
   tv.tv_usec = 0;
   nfds = pipefd[1] + 1;

   do {
      rc = select(nfds, &readfds, &writefds, NULL, &tv);
   } while (rc < 0 && errno == EINTR);

   if (rc < 0) {
      printf("[parent] ERROR: select() failed with: %s\n", strerror(errno));
      failed = 1;
      goto wait_child_and_return;
   }

   if (rc == 0) {
      printf("[parent] ERROR: select() timed out (unexpected)\n");
      failed = 1;
      goto wait_child_and_return;
   }

   if (rc != 1) {
      printf("[parent] ERROR: select() returned %d (expected: 1)\n", rc);
      failed = 1;
      goto wait_child_and_return;
   }

   if (!FD_ISSET(pipefd[1], &writefds)) {
      printf("[parent] ERROR: pipefd[1] is NOT set in writefds\n");
      failed = 1;
      goto wait_child_and_return;
   }

   printf("[parent] selected() completed as expected\n");

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

/* Test select() with tv != NULL, but tv_sec = tv_usec = 0 */
int cmd_select2(int argc, char **argv)
{
   struct timeval tv;
   fd_set readfds;
   int nfds, pipefd[2];
   char buf[64];
   int rc;

   printf("Creating a pipe...\n");
   rc = pipe(pipefd);

   if (rc < 0) {
      printf("pipe() failed. Error: %s\n", strerror(errno));
      return 1;
   }

   FD_ZERO(&readfds);
   FD_SET(pipefd[0], &readfds);
   tv.tv_sec = 0;
   tv.tv_usec = 0;
   nfds = pipefd[0] + 1;

   printf("Running select()...\n");
   rc = select(nfds, &readfds, NULL, NULL, &tv);

   if (rc < 0) {
      perror("select");
      return 1;
   }

   printf("select() returned (immediately): %d\n", rc);

   if (rc != 0) {
      printf("select() did *not* return 0 as expected\n");
      return 1;
   }

   printf("Write something on the pipe\n");

   rc = write(pipefd[1], "hello", 5);
   DEVSHELL_CMD_ASSERT(rc == 5);

   FD_ZERO(&readfds);
   FD_SET(pipefd[0], &readfds);
   tv.tv_sec = 0;
   tv.tv_usec = 0;
   nfds = pipefd[0] + 1;

   printf("Running select()\n");
   rc = select(nfds, &readfds, NULL, NULL, &tv);

   if (rc < 0) {
      perror("select");
      return 1;
   }

   printf("select() returned (immediately): %d\n", rc);

   if (rc != 1) {
      printf("select() did return 1 as expected\n");
      return 1;
   }

   if (!FD_ISSET(pipefd[0], &readfds)) {
      printf("[parent] ERROR: pipefd[0] is NOT set in readfds\n");
      return 1;
   }

   printf("Everything is alright, pipefd[0] is set in readfds\n");

   rc = read(pipefd[0], buf, sizeof(buf));
   DEVSHELL_CMD_ASSERT(rc > 0);

   buf[rc] = 0;
   printf("Got from the pipe: '%s'\n", buf);

   close(pipefd[0]);
   close(pipefd[1]);
   return 0;
}
