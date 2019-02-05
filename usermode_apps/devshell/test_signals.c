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

static int
test_sig(void (*child_func)(void), int expected_sig, int expected_code)
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
      child_func();
      exit(0);
   }

   rc = waitpid(-1, &wstatus, 0);

   if (rc != child_pid) {
      printf("waitpid returned %d instead of child's pid: %d\n", rc);
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

static void child_generate_sigabrt(void)
{
   abort();
}

static void child_generate_and_ignore_sigint(void)
{
   signal(SIGINT, SIG_IGN); /* ignore SIGINT */
   raise(SIGINT);           /* expect nothing to happen */
   exit(0);
}

int cmd_sigsegv1(int argc, char **argv)
{
   return test_sig(child_generate_gpf, SIGSEGV, 0);
}

int cmd_sigsegv2(int argc, char **argv)
{
   return test_sig(child_generate_non_cow_page_fault, SIGSEGV, 0);
}

int cmd_sigill(int argc, char **argv)
{
   return test_sig(child_generate_sigill, SIGILL, 0);
}

int cmd_sigfpe(int argc, char **argv)
{
   return test_sig(child_generate_sigfpe, SIGFPE, 0);
}

int cmd_sigabrt(int argc, char **argv)
{
   return test_sig(child_generate_sigabrt, SIGABRT, 0);
}

int cmd_sig1(int argc, char **argv)
{
   return test_sig(child_generate_and_ignore_sigint, 0, 0);
}
