/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/syscall.h>
#include <sys/mman.h>

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

   tv.tv_sec = 10;
   tv.tv_usec = 0;

   ret = select(nfds, &readfds, NULL, NULL, &tv);

   if (ret < 0) {
      perror("select");
      return 1;
   }

   printf("select returned %d\n", ret);

   for (int i = 0; i < nfds; i++) {
      if (FD_ISSET(i, &readfds))
         printf("fd %d is read-ready\n", i);
   }

   printf("tv: %lu.%lu sec\n", tv.tv_sec, tv.tv_usec / 1000);
   return 0;
}
