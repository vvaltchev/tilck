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

int cmd_sig_ignore(int argc, char **argv)
{
   return test_sig(child_generate_and_ignore_sigint, NULL, 0, 0);
}

static int compare_sig_tests(int id, sigset_t *set, sigset_t *oldset)
{
   for (int i = 1; i < 32; i++) {

      if (sigismember(set, i) != sigismember(oldset, i)) {

         printf(
            "[case %d], set[%d]: %d != oldset[%d]: %d\n",
            id, i, sigismember(set, i), i, sigismember(oldset, i)
         );
         return 1;
      }
   }

   return 0;
}

int cmd_sigmask(int argc, char **argv)
{
   int rc;
   sigset_t set, oldset;

   sigemptyset(&set);

   rc = sigprocmask(SIG_SETMASK, &set, NULL);
   DEVSHELL_CMD_ASSERT(rc == 0);

   rc = sigprocmask(0 /* how ignored */, NULL, &oldset);
   DEVSHELL_CMD_ASSERT(rc == 0);

   if (compare_sig_tests(0, &set, &oldset))
      return 1;

   sigemptyset(&set);
   rc = sigprocmask(SIG_SETMASK, &set, NULL);
   DEVSHELL_CMD_ASSERT(rc == 0);

   rc = sigprocmask(0 /* how ignored */, NULL, &oldset);
   DEVSHELL_CMD_ASSERT(rc == 0);

   if (compare_sig_tests(1, &set, &oldset))
      return 1;

   sigemptyset(&set);
   sigaddset(&set, 5);
   sigaddset(&set, 10);
   sigaddset(&set, 12);
   sigaddset(&set, 20);

   rc = sigprocmask(SIG_BLOCK, &set, NULL);
   DEVSHELL_CMD_ASSERT(rc == 0);

   rc = sigprocmask(0 /* how ignored */, NULL, &oldset);
   DEVSHELL_CMD_ASSERT(rc == 0);

   if (compare_sig_tests(2, &set, &oldset))
      return 1;

   sigemptyset(&set);
   sigaddset(&set, 12);

   rc = sigprocmask(SIG_UNBLOCK, &set, NULL);
   DEVSHELL_CMD_ASSERT(rc == 0);

   sigdelset(&oldset, 12);
   memcpy(&set, &oldset, sizeof(sigset_t));

   rc = sigprocmask(0 /* how ignored */, NULL, &oldset);
   DEVSHELL_CMD_ASSERT(rc == 0);

   if (compare_sig_tests(3, &set, &oldset))
      return 1;

   return 0;
}

static volatile bool test_got_sig[_NSIG];

void child_sig_handler(int signum)
{
   printf("child: handle signal: %d\n", signum);

   if (!is_stack_aligned_16()) {
      printf("child: stack is NOT aligned at 16-bytes boundary\n");
      exit(1);
   }

   test_got_sig[signum] = true;
   fflush(stdout);
}

static bool got_all_signals(int n)
{
   switch (n) {

      case 1:
         return test_got_sig[SIGHUP];

      case 2:
         return test_got_sig[SIGHUP] && test_got_sig[SIGINT];

      default:
         abort();
   }
}

static void test_sig_child_body(int n, bool busy_loop)
{
   /*
    * Special variables FORCED to be on the stack by using "volatile" and the
    * magic DO_NOT_OPTIMIZE_AWAY(). We need them to check that the kernel
    * restored correctly the stack pointer after the signal handler run.
    */
   volatile unsigned magic1 = 0xcafebabe;
   volatile unsigned magic2 = 0x11223344;
   DO_NOT_OPTIMIZE_AWAY(magic1);
   DO_NOT_OPTIMIZE_AWAY(magic2);

   memset((void *)test_got_sig, 0, sizeof(test_got_sig));

   signal(SIGHUP, &child_sig_handler);
   signal(SIGINT, &child_sig_handler);

   if (busy_loop) {

      for (int i = 0; i < 100*1000*1000; i++) {
         if (got_all_signals(n))
            break;
      }

   } else {

      pause();
   }

   if (!got_all_signals(n)) {

      int count = 0;

      if (n >= 1) {
         count += test_got_sig[SIGHUP];

         if (n >= 2)
            count += test_got_sig[SIGINT];
      }

      if (busy_loop)
         printf("child: timeout!\n");

      printf("child: didn't run handlers for all expected signals [%d/%d]\n",
             count, n);

      fflush(stdout);
      exit(1);
   }

   if (magic1 != 0xcafebabe || magic2 != 0x11223344) {
      printf("child: magic variables got corrupted!\n");
      exit(1);
   }

   exit(0);
}

static int test_sig_n(int n, bool busy_loop, int exp_term_sig)
{
   int code, term_sig;
   int child_pid;
   int wstatus;
   int rc;

   DEVSHELL_CMD_ASSERT(n == 1 || n == 2);
   child_pid = fork();

   if (!child_pid) {
      test_sig_child_body(n, busy_loop);
   }

   usleep(100 * 1000);

   if (exp_term_sig) {

      kill(child_pid, exp_term_sig);

   } else {

      kill(child_pid, SIGHUP);

      if (n >= 2)
         kill(child_pid, SIGINT);
   }

   rc = waitpid(child_pid, &wstatus, 0);

   if (rc != child_pid) {
      printf("waitpid returned %d instead of child's pid: %d\n", rc, child_pid);
      return 1;
   }

   code = WEXITSTATUS(wstatus);
   term_sig = WTERMSIG(wstatus);

   printf("parent: child exit code: %d, term_sig: %d\n", code, term_sig);

   if (exp_term_sig) {

      if (term_sig != exp_term_sig) {
         printf("FAIL: expected child to be killed by sig %d. It did not.\n",
                exp_term_sig);
         return 1;
      }

   } else {

      if (term_sig || code != 0) {
         printf("FAIL: expected child to exit gracefully. It did not.\n");
         return 1;
      }
   }

   return 0;
}

/* Test delivery of single signal durning syscall */
int cmd_sig1(int argc, char **argv)
{
   return test_sig_n(1, false, 0);
}

/* Test delivery of two signals durning syscall */
int cmd_sig2(int argc, char **argv)
{
   return test_sig_n(2, false, 0);
}

/* Test signal delivery while user space is running */
int cmd_sig3(int argc, char **argv)
{
   return test_sig_n(1, true, 0);
}

/* Test killing signal delivery while user space is running */
int cmd_sig4(int argc, char **argv)
{
   return test_sig_n(1, true, SIGKILL);
}
