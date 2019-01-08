/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/mman.h>

#include "devshell.h"
#include "sysenter.h"

/*
 * Call waitpid() after the child exited.
 */
int cmd_waitpid1(int argc, char **argv)
{
   int wstatus;
   pid_t pid;

   int child_pid = fork();

   if (child_pid < 0) {
      printf("fork() failed\n");
      return 1;
   }

   if (!child_pid) {
      // This is the child, just exit
      printf("child: exit\n");
      exit(23);
   }

   printf("Created child with pid: %d\n", child_pid);

   /* Wait for the child to exit */
   usleep(100*1000);

   /* Now, let's see call waitpid() after the child exited */
   pid = waitpid(child_pid, &wstatus, 0);

   if (!WIFEXITED(wstatus)) {

      printf("[pid: %d] PARENT: the child %d did NOT exited normally\n",
             getpid(), pid);

      return 1;
   }

   int exit_code = WEXITSTATUS(wstatus);
   printf("waitpid() returned %d, exit code: %d\n", pid, exit_code);

   if (pid != child_pid) {
      printf("Expected waitpid() to return child's pid (got: %d)\n", pid);
      return 1;
   }

   if (exit_code != 23) {
      printf("Expected the exit code to be 23 (got: %d)\n", exit_code);
      return 1;
   }

   return 0;
}

/* waitpid(-1): wait any child to exit */
int cmd_waitpid2(int argc, char **argv)
{
   const int child_count = 3;

   int pids[child_count];
   int wstatus;
   pid_t pid;

   for (int i = 0; i < child_count; i++) {

      int child_pid = fork();

      if (child_pid < 0) {
         printf("fork() failed\n");
         return 1;
      }

      if (!child_pid) {
         printf("[pid: %d] child: exit (%d)\n", getpid(), 10 + i);
         usleep((child_count - i) * 100*1000);
         exit(10 + i); // exit from the child
      }

      pids[i] = child_pid;
   }

   usleep(120 * 1000);

   for (int i = child_count-1; i >= 0; i--) {

      printf("[pid: %d] PARENT: waitpid(-1)\n", getpid());
      pid = waitpid(-1, &wstatus, 0);

      if (!WIFEXITED(wstatus)) {

         printf("[pid: %d] PARENT: the child %d did NOT exited normally\n",
                 getpid(), pid);

         return 1;
      }

      int exit_code = WEXITSTATUS(wstatus);

      printf("[pid: %d] PARENT: waitpid() returned %d, exit code: %d\n",
             getpid(), pid, exit_code);

      if (pid != pids[i]) {
         printf("Expected waitpid() to return %d (got: %d)\n", pids[i], pid);
         return 1;
      }

      if (exit_code != 10+i) {
         printf("Expected the exit code to be %d (got: %d)\n", 10+i, exit_code);
         return 1;
      }
   }

   return 0;
}

/* wait on any child after they exit */
int cmd_waitpid3(int argc, char **argv)
{
   int wstatus;
   pid_t pid;

   int child_pid = fork();

   if (child_pid < 0) {
      printf("fork() failed\n");
      return 1;
   }

   if (!child_pid) {
      // This is the child, just exit
      printf("child: exit\n");
      exit(23); // exit from the child
   }

   printf("Created child with pid: %d\n", child_pid);

   /* Wait for the child to exit */
   usleep(100*1000);

   /* Now, let's see call waitpid() after the child exited */
   pid = waitpid(-1, &wstatus, 0);

   if (!WIFEXITED(wstatus)) {

      printf("[pid: %d] PARENT: the child %d did NOT exited normally\n",
             getpid(), pid);

      return 1;
   }

   int exit_code = WEXITSTATUS(wstatus);
   printf("waitpid() returned %d, exit code: %d\n", pid, exit_code);

   if (pid != child_pid) {
      printf("Expected waitpid() to return child's pid (got: %d)\n", pid);
      return 1;
   }

   if (exit_code != 23) {
      printf("Expected the exit code to be 23 (got: %d)\n", exit_code);
      return 1;
   }

   return 0;
}
