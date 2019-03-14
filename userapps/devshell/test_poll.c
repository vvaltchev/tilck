/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
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
