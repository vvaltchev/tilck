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

static void
print_waitpid_change(int child, int wstatus)
{
   int code = WEXITSTATUS(wstatus);
   int sig = WTERMSIG(wstatus);

   if (WIFSTOPPED(wstatus))
      printf("[   parent   ] child %d: STOPPED\n", child);
   else if (WIFCONTINUED(wstatus))
      printf("[   parent   ] child %d: CONTINUED\n", child);
   else if (WIFEXITED(wstatus))
      printf("[   parent   ] child %d: EXITED with %d\n", child, code);
   else if (WIFSIGNALED(wstatus))
      printf("[   parent   ] child %d: KILLED by sig: %d\n", child, sig);
   else
      printf("[   parent   ] child %d: UNKNOWN status change!\n", child);

   fflush(stdout);
}

static void
call_waitpid(int *pid, int *wstatus, int *children)
{
   *pid = waitpid(-1, wstatus, WUNTRACED | WCONTINUED);
   DEVSHELL_CMD_ASSERT(*pid > 0);
   print_waitpid_change(*pid == children[0] ? 0 : 1, *wstatus);
}

/*
 * Wait on children getting SIGSTOP and SIGCONT
 */
int cmd_wpid5(int argc, char **argv)
{
   pid_t children[2];
   int pid, wstatus;

   children[0] = fork();

   if (children[0] < 0) {
      printf("[   parent   ] fork() failed\n");
      return 1;
   }

   if (!children[0]) {

      /* child 0's body */
      printf("[child 0] Hello from pid %d\n", getpid());
      fflush(stdout);

      for (int i = 0; i < 10; i++) {
         printf("[child 0] i = %d\n", i);
         fflush(stdout);
         usleep(100 * 1000);
      }

      printf("[child 0] exit\n");
      fflush(stdout);
      exit(0);
   }

   printf("[   parent   ] children[0] pid: %d\n", children[0]);
   fflush(stdout);

   children[1] = fork();

   if (children[1] < 0) {
      printf("[   parent   ] fork() failed\n");
      kill(children[0], SIGKILL);
      return 1;
   }

   if (!children[1]) {

      /* child 1's body */
      printf("[child 1] Hello from pid %d\n", getpid());
      fflush(stdout);

      printf("[child 1] Wait some time...\n");
      fflush(stdout);
      usleep(250 * 1000);

      printf("[child 1] Send SIGSTOP to child 0\n");
      fflush(stdout);
      kill(children[0], SIGSTOP);

      printf("[child 1] Wait some time...\n");
      fflush(stdout);
      usleep(250 * 1000);

      printf("[child 1] Send SIGCONT to child 0\n");
      fflush(stdout);
      kill(children[0], SIGCONT);

      printf("[child 1] Wait some time...\n");
      fflush(stdout);
      usleep(50 * 1000);

      printf("[child 1] exit\n");
      fflush(stdout);
      exit(0);
   }

   /* Expect child 0 stopped */
   call_waitpid(&pid, &wstatus, children);
   DEVSHELL_CMD_ASSERT(pid == children[0]);
   DEVSHELL_CMD_ASSERT(WIFSTOPPED(wstatus));

   /* Expect child 0 continued */
   call_waitpid(&pid, &wstatus, children);
   DEVSHELL_CMD_ASSERT(pid == children[0]);
   DEVSHELL_CMD_ASSERT(WIFCONTINUED(wstatus));

   /* Expect that child 1 exited */
   call_waitpid(&pid, &wstatus, children);
   DEVSHELL_CMD_ASSERT(pid == children[1]);
   DEVSHELL_CMD_ASSERT(WIFEXITED(wstatus));

   /* Expect that child 0 exited */
   call_waitpid(&pid, &wstatus, children);
   DEVSHELL_CMD_ASSERT(pid == children[0]);
   DEVSHELL_CMD_ASSERT(WIFEXITED(wstatus));

   return 0;
}
