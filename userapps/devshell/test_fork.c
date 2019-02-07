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

static int sysenter_fork(void)
{
   return sysenter_call0(2 /* fork */);
}

static int fork_test(int (*fork_func)(void))
{
   printf("Running infinite loop..\n");

   unsigned n = 1;
   int FORK_TEST_ITERS_hits_count = 0;
   bool inchild = false;
   bool exit_on_next_FORK_TEST_ITERS_hit = false;

   while (true) {

      if (!(n % FORK_TEST_ITERS)) {

         printf("[PID: %i] FORK_TEST_ITERS hit!\n", getpid());

         if (exit_on_next_FORK_TEST_ITERS_hit) {
            break;
         }

         FORK_TEST_ITERS_hits_count++;

         if (FORK_TEST_ITERS_hits_count == 1) {

            printf("forking..\n");

            int pid = fork_func();

            printf("Fork returned %i\n", pid);

            if (pid == 0) {
               printf("############## I'm the child!\n");
               inchild = true;
            } else {
               printf("############## I'm the parent, child's pid = %i\n", pid);
               printf("[parent] waiting the child to exit...\n");
               int wstatus=0;
               int p = waitpid(pid, &wstatus, 0);
               printf("[parent] child (pid: %i) exited with status: %i!\n",
                      p, WEXITSTATUS(wstatus));
               exit_on_next_FORK_TEST_ITERS_hit = true;
            }

         }

         if (FORK_TEST_ITERS_hits_count == 2 && inchild) {
            printf("child: 2 iter hits, exit!\n");
            exit(123); // exit from the child
         }
      }

      n++;
   }

   return 0;
}

int cmd_fork(int argc, char **argv)
{
   return fork_test(&fork);
}

int cmd_fork_perf(int argc, char **argv)
{
   const int iters = 150000;
   int wstatus, child_pid;
   unsigned long long start, duration;

   start = RDTSC();

   for (int i = 0; i < iters; i++) {

      child_pid = fork();

      if (child_pid < 0) {
         printf("fork() failed: %s\n", strerror(errno));
         return 1;
      }

      if (!child_pid)
         exit(0); // exit from the child

      waitpid(child_pid, &wstatus, 0);
   }


   duration = RDTSC() - start;
   printf("duration: %llu\n", duration/iters);
   return 0;
}

int cmd_fork_se(int argc, char **argv)
{
   return fork_test(&sysenter_fork);
}
