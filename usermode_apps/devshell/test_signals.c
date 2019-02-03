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

int test_sig(void (*child_func)(void), int expected_sig)
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
      child_func();
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

   if (term_sig != expected_sig) {
      printf("ERROR: expected child exit due to signal "
             "%d, got: %d\n", expected_sig, term_sig);
      return 1;
   }

   printf("The child exited with signal %d, as expected.\n", expected_sig);
   return 0;
}

static void child_generate_gpf(void)
{
   /* cause a general fault protection */
   asmVolatile("hlt");
}

static void child_generate_non_cow_page_fault(void)
{
   /* cause non-CoW page fault */
   *(volatile int *)0xabc = 25;
}

static void child_generate_sigill(void)
{
   /* trigger an illegal instruction fault */
   asmVolatile(".byte 0x0f\n\t"
               ".byte 0x0b\n\t");
}

static void child_generate_sigfpe(void)
{
   static int zero_val;
   static int val;

   if (!val)
      val = 35 / zero_val;
}

int cmd_sigsegv1(int argc, char **argv)
{
   return test_sig(child_generate_gpf, SIGSEGV);
}

int cmd_sigsegv2(int argc, char **argv)
{
   return test_sig(child_generate_non_cow_page_fault, SIGSEGV);
}

int cmd_sigill(int argc, char **argv)
{
   return test_sig(child_generate_sigill, SIGILL);
}

int cmd_sigfpe(int argc, char **argv)
{
   return test_sig(child_generate_sigfpe, SIGFPE);
}
