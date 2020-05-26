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
#include <sys/syscall.h>
#include <sys/mman.h>

#include "devshell.h"

int test_sig(void (*child_func)(void *),
             void *arg,
             int expected_sig,
             int expected_code)
{
   int code, term_sig;
   int child_pid;
   int wstatus;
   int rc;

   child_pid = fork();

   if (child_pid < 0) {
      printf("fork() failed\n");
      return 1;
   }

   if (!child_pid) {
      child_func(arg);
      exit(0);
   }

   rc = waitpid(-1, &wstatus, 0);

   if (rc != child_pid) {
      printf("waitpid returned %d instead of child's pid: %d\n", rc, child_pid);
      return 1;
   }

   code = WEXITSTATUS(wstatus);
   term_sig = WTERMSIG(wstatus);

   if (expected_sig > 0) {

      if (code != 0) {
         printf("ERROR: expected child to exit with 0, got: %d\n", code);
         return 1;
      }

      if (term_sig != expected_sig) {
         printf("ERROR: expected child exit due to signal "
                "%d, instead got terminated by: %d\n", expected_sig, term_sig);
         return 1;
      }

      printf("The child exited with signal %d, as expected.\n", expected_sig);

   } else {

      if (term_sig != 0) {
         printf("ERROR: expected child to exit with code %d, "
                "it got terminated with signal: %d\n", expected_code, term_sig);
         return 1;
      }

      if (code != expected_code) {
         printf("ERROR: expected child exit with "
                "code %d, got: %d\n", expected_code, code);
         return 1;
      }

      printf("The child exited with code %d, as expected.\n", expected_code);
   }
   return 0;
}

static void child_generate_gpf(void *unused)
{
   /* cause a general fault protection */
   asmVolatile("hlt");
}

static void child_generate_non_cow_page_fault(void *unused)
{
   /* cause non-CoW page fault */
   *(volatile int *)0xabc = 25;
}

static void child_generate_sigill(void *unused)
{
   /* trigger an illegal instruction fault */
   asmVolatile(".byte 0x0f\n\t"
               ".byte 0x0b\n\t");
}

static void child_generate_sigfpe(void *unused)
{
   volatile int zero_val = 0;
   volatile int val = 35 / zero_val;

   printf("ERROR: val is %d\n", val);
   exit(1);
}

static void child_generate_sigabrt(void *unused)
{
   abort();
}

static void child_generate_and_ignore_sigint(void *unused)
{
   signal(SIGINT, SIG_IGN); /* ignore SIGINT */
   raise(SIGINT);           /* expect nothing to happen */
   exit(0);
}

int cmd_sigsegv1(int argc, char **argv)
{
   return test_sig(child_generate_gpf, NULL, SIGSEGV, 0);
}

int cmd_sigsegv2(int argc, char **argv)
{
   return test_sig(child_generate_non_cow_page_fault, NULL, SIGSEGV, 0);
}

int cmd_sigill(int argc, char **argv)
{
   return test_sig(child_generate_sigill, NULL, SIGILL, 0);
}

int cmd_sigfpe(int argc, char **argv)
{
   return test_sig(child_generate_sigfpe, NULL, SIGFPE, 0);
}

int cmd_sigabrt(int argc, char **argv)
{
   return test_sig(child_generate_sigabrt, NULL, SIGABRT, 0);
}

int cmd_sig1(int argc, char **argv)
{
   return test_sig(child_generate_and_ignore_sigint, NULL, 0, 0);
}
