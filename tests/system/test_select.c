/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/select.h>

#include "devshell.h"

int cmd_select1(int argc, char **argv)
{
   struct timeval tv;
   fd_set readfds;
   int ret;
   int nfds = 0 /* stdin */ + 1;

   /* Wait on stdin for input. */
   FD_ZERO(&readfds);
   FD_SET(0, &readfds);

   tv.tv_sec = 5;
   tv.tv_usec = 0;

   ret = select(nfds, &readfds, NULL, NULL, &tv);

   if (ret < 0) {
      perror("select");
      return 1;
   }

   printf("select() returned %d\n", ret);

   for (int i = 0; i < nfds; i++) {
      if (FD_ISSET(i, &readfds))
         printf("fd %d is read-ready\n", i);
   }

   printf("tv: %lu sec + %lu ms\n", tv.tv_sec, tv.tv_usec / 1000);
   return 0;
}

/* Test select() with tv != NULL, but tv_sec = tv_usec = 0 */
int cmd_select2(int argc, char **argv)
{
   struct timeval tv;
   fd_set readfds;
   int ret;
   int nfds = 0 /* stdin */ + 1;

   while (true) {

      /* Wait on stdin for input. */
      FD_ZERO(&readfds);
      FD_SET(0, &readfds);

      tv.tv_sec = 0;
      tv.tv_usec = 0;

      printf("call select(..., tv = {0, 0})\n");
      ret = select(nfds, &readfds, NULL, NULL, &tv);

      if (ret < 0) {
         perror("select");
         return 1;
      }

      if (!FD_ISSET(0, &readfds)) {
         printf("NO fd is ready, just wait\n");
         sleep(1);
         continue;
      }

      printf("*** fd 0 is ready: end ***\n");
      break;
   }

   return 0;
}
