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
int cmd_wpid1(int argc, char **argv)
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
int cmd_wpid2(int argc, char **argv)
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
int cmd_wpid3(int argc, char **argv)
{
   int wstatus;
   pid_t pid;

   int child_pid = fork();

   if (child_pid < 0) {
      printf("fork() failed\n");
      return 1;
   }

   if (!child_pid) {
      /* This is the child, just exit with a special code */
      printf("child: exit\n");
      exit(23);
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


/*
 * Test the case of a parent dying before its children.
 */
int cmd_wpid4(int argc, char **argv)
{
   pid_t pid;
   int wstatus;
   int child_pid;
   bool failed = false;

   printf("[grandparent] my pid: %d\n", getpid());

   child_pid = fork();

   if (child_pid < 0) {
      printf("fork() failed\n");
      return 1;
   }

   if (!child_pid) {

      /* in the child: now create other children and die before them */

      int grand_child1, grand_child2;

      grand_child1 = fork();

      if (grand_child1 < 0) {
         printf("fork() failed\n");
         exit(1);
      }

      if (!grand_child1) {
         usleep(50*1000);
         exit(10);
      }

      printf("[parent] child 1: %d\n", grand_child1);

      grand_child2 = fork();

      if (grand_child2 < 0) {
         printf("fork() failed\n");
         exit(1);
      }

      if (!grand_child2) {
         usleep(100*1000);
         exit(11);
      }

      printf("[parent] child 2: %d\n", grand_child2);
      printf("[parent] exit\n");
      exit(0);
   }

   /* in the grandparent: wait for any child */
   printf("[grandparent] child pid: %d\n", child_pid);

   while ((pid = waitpid(-1, &wstatus, 0)) > 0) {

      int code = WEXITSTATUS(wstatus);
      printf("[grandparent] waitpid(): child %d exited with %d\n", pid, code);

      if (code != 0)
         failed = true;
   }

   printf("[grandparent] exit (failed: %d)\n", failed);
   return failed ? 1 : 0;
}


/* Test child exit with SIGSEGV */
int cmd_wpid5(int argc, char **argv)
{
   int child_pid;
   int wstatus;
   int rc;

   child_pid = fork();

   if (child_pid < 0) {
      printf("fork() failed\n");
      return 1;
   }

   if (!child_pid) {

      /* cause a general fault protection */
      __asm__ volatile("hlt");
      exit(0);
   }

   rc = waitpid(-1, &wstatus, 0);

   if (rc != child_pid) {
      printf("waitpid returned %d instead of child's pid: %d\n", rc);
      return 1;
   }

   int code = WEXITSTATUS(wstatus);
   int term_sig = WTERMSIG(wstatus);

   if (code != 0) {
      printf("ERROR: expected child to exit with 0, got: %d\n", code);
      return 1;
   }

   if (term_sig != SIGSEGV) {
      printf("ERROR: expected child exit due to signal "
             "SIGSEGV (%d), got: %d\n", SIGSEGV, term_sig);
      return 1;
   }

   printf("The child exited with SIGSEGV, as expected.\n");
   return 0;
}
