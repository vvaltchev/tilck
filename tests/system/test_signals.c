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

static void child_generate_gpf(void *unused)
{
   /* cause a general fault protection */
   asmVolatile("hlt");
}

static void child_generate_non_cow_page_fault(void *unused)
{
   /* cause non-CoW page fault */
   int val = 25;
   memcpy((int *)0xabc, &val, sizeof(val));
}

static void child_generate_sigill(void *unused)
{
   execute_illegal_instruction();
}

static void child_generate_sigfpe(void *unused)
{
   volatile int zero_val = 0;
   volatile int val = 35 / zero_val;

   printf("FAIL: expected SIGFPE, got val: %d\n", val);
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

struct generic_child_ctx {

   int sig;
   void *handler;
   int masked_sig1;
   int masked_sig2;
   void (*main_action_cb)(struct generic_child_ctx *);
   void (*after_unmask_cb)(void);
};

static void gc_action_pause(struct generic_child_ctx *ctx)
{
   pause();
}

static void gc_action_self_kill(struct generic_child_ctx *ctx)
{
   kill(getpid(), ctx->sig);
}

static void gc_action_gen_gpf(struct generic_child_ctx *ctx)
{
   child_generate_gpf(NULL);
}

static void gc_action_gen_page_fault(struct generic_child_ctx *ctx)
{
   child_generate_non_cow_page_fault(NULL);
}

void mask_signal(int sig)
{
   sigset_t set;
   int rc;

   sigemptyset(&set);
   sigaddset(&set, sig);

   rc = sigprocmask(SIG_BLOCK, &set, NULL);

   if (rc != 0) {
      printf("FAIL: sigprocmask() failed with %s (%d)\n",
             strerror(errno), errno);
      exit(1);
   }
}

void unmask_signal(int sig)
{
   sigset_t set;
   int rc;

   sigemptyset(&set);
   sigaddset(&set, sig);

   rc = sigprocmask(SIG_UNBLOCK, &set, NULL);

   if (rc != 0) {
      printf("FAIL: sigprocmask() failed with %s (%d)\n",
             strerror(errno), errno);
      exit(1);
   }
}

static void
generic_child_do_mask(struct generic_child_ctx *ctx, sigset_t *set)
{
   int rc;

   sigemptyset(set);

   if (ctx->masked_sig1)
      sigaddset(set, ctx->masked_sig1);

   if (ctx->masked_sig2)
      sigaddset(set, ctx->masked_sig2);

   rc = sigprocmask(SIG_BLOCK, set, NULL);

   if (rc != 0) {
      printf("FAIL[1]: sigprocmask() failed with: %s (%d)\n",
               strerror(errno), errno);
      exit(1);
   }
}

static void
generic_child_check_pending(struct generic_child_ctx *ctx)
{
   sigset_t pending_set;
   int rc = sigpending(&pending_set);

   if (rc != 0) {
      printf("FAIL[2]: sigpending failed with: %s (%d)\n",
               strerror(errno), errno);
      exit(1);
   }

   if (ctx->masked_sig1) {
      if (!sigismember(&pending_set, ctx->masked_sig1)) {
         printf("FAIL[3]: masked_sig1 is NOT pending\n");
         exit(1);
      }
   }

   if (ctx->masked_sig2) {
      if (!sigismember(&pending_set, ctx->masked_sig2)) {
         printf("FAIL[3]: masked_sig2 is NOT pending\n");
         exit(1);
      }
   }
}

static void
generic_child(void *arg)
{
   struct generic_child_ctx *ctx = arg;
   sigset_t set;
   int rc;

   signal(ctx->sig, ctx->handler);

   if (ctx->masked_sig1 || ctx->masked_sig2)
      generic_child_do_mask(ctx, &set);

   if (ctx->main_action_cb)
      ctx->main_action_cb(ctx);
   else
      pause();

   if (ctx->after_unmask_cb) {

      generic_child_check_pending(ctx);

      /*
       * Run the after_mask_cb callback. The purpose of this callback is to
       * alter the global state used by signal handlers in a way to allow the
       * test to distinguish the case where the signal handler has been run
       * despite being masked (bug) from the one where the signal handler has
       * been run after the signal handler is unblocked here below.
       */
      ctx->after_unmask_cb();

      rc = sigprocmask(SIG_UNBLOCK, &set, NULL);

      if (rc != 0) {
         printf("FAIL[3]: sigprocmask() failed with: %s (%d)\n",
                strerror(errno), errno);
         exit(1);
      }
   }

   exit(0);
}

int
test_sig(void (*child_func)(void *),
         void *arg,
         int expected_sig,
         int expected_code,
         int signal_to_send)
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

   if (signal_to_send) {

      printf("parent: wait 100ms...\n");
      usleep(100 * 1000);

      printf("parent: send signal %d to child\n", signal_to_send);
      kill(child_pid, signal_to_send);
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
         printf("FAIL: expected child to exit with 0, got: %d\n", code);
         return 1;
      }

      if (term_sig != expected_sig) {
         printf("FAIL: expected child exit due to signal "
                "%d, instead got terminated by: %d\n", expected_sig, term_sig);
         return 1;
      }

      printf("parent: the child exited with signal %d, as expected.\n",
             expected_sig);

   } else {

      if (term_sig != 0) {
         printf("FAIL: expected child to exit with code %d, "
                "it got terminated with signal: %d\n", expected_code, term_sig);
         return 1;
      }

      if (code != expected_code) {
         printf("FAIL: expected child exit with "
                "code %d, got: %d\n", expected_code, code);
         return 1;
      }

      printf("parent: the child exited with code %d, as expected.\n",
             expected_code);
   }
   return 0;
}

/* Trigger SIGSEGV it with a real fault */
int cmd_sigsegv0(int argc, char **argv)
{
   return test_sig(child_generate_gpf, NULL, SIGSEGV, 0, 0);
}

/* Mask SIGSEGV and then trigger it with a real GPF */
int cmd_sigsegv1(int argc, char **argv)
{
   mask_signal(SIGSEGV);
   return test_sig(child_generate_gpf, NULL, SIGSEGV, 0, 0);
}

/* Mask SIGSEGV and then trigger it with a real page-fault */
int cmd_sigsegv2(int argc, char **argv)
{
   mask_signal(SIGSEGV);
   return test_sig(child_generate_non_cow_page_fault, NULL, SIGSEGV, 0, 0);
}

/* Mask SIGILL and then trigger it with a real fault */
int cmd_sigill(int argc, char **argv)
{
   mask_signal(SIGILL);
   return test_sig(child_generate_sigill, NULL, SIGILL, 0, 0);
}

/* Mask SIGFPE and then trigger it with a real fault */
int cmd_sigfpe(int argc, char **argv)
{
   mask_signal(SIGFPE);
   return test_sig(child_generate_sigfpe, NULL, SIGFPE, 0, 0);
}

int cmd_sigabrt(int argc, char **argv)
{
   return test_sig(child_generate_sigabrt, NULL, SIGABRT, 0, 0);
}

int cmd_sig_ignore(int argc, char **argv)
{
   return test_sig(child_generate_and_ignore_sigint, NULL, 0, 0, 0);
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
   volatile unsigned long magic1 = 0xcafebabe;
   volatile unsigned long magic2 = 0x11223344;
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

   /* --- parent --- */

   /*
    * Sleep a bit in order to give some time to the child to enter in the busy
    * loop. NOTE: this is an extremely poor way of synchronizing two tasks,
    * even if it's simple and "just works" for tests.
    *
    * TODO: impl. a proper synchronization mechanism for sig3 and sig4 tests.
    */
   usleep(25 * 1000);

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

static int sig_handler_call_exit_code = 42;

static void sig_handler_call_exit(int sig)
{
   exit(sig_handler_call_exit_code);
}

static void increase_call_exit_code(void)
{
   sig_handler_call_exit_code++;
}

/* Test that exit() works in signal handlers */
int cmd_sig5(int argc, char **argv)
{
   struct generic_child_ctx ctx = {
      .sig = SIGTERM,
      .handler = &sig_handler_call_exit,
      .masked_sig1 = 0,
      .masked_sig2 = 0,
      .main_action_cb = &gc_action_pause,
   };

   return test_sig(
      &generic_child,
      &ctx,
      0,
      42,
      SIGTERM
   );
}

static void sig_handler_self_kill(int sig)
{
   kill(getpid(), SIGQUIT);
}

/* Test that kill() works in signal handlers */
int cmd_sig6(int argc, char **argv)
{
   struct generic_child_ctx ctx = {
      .sig = SIGTERM,
      .handler = &sig_handler_self_kill,
      .masked_sig1 = 0,
      .masked_sig2 = 0,
      .main_action_cb = &gc_action_pause,
   };

   return test_sig(
      &generic_child,
      &ctx,
      SIGQUIT,
      0,
      SIGTERM
   );
}

/*
 * Test that with sigprocmask() a signal handler won't be executed until
 * the signal in unmasked.
 */
int cmd_sig7(int argc, char **argv)
{
   struct generic_child_ctx ctx = {
      .sig = SIGTERM,
      .handler = &sig_handler_call_exit,
      .masked_sig1 = SIGTERM,
      .masked_sig2 = 0,
      .after_unmask_cb = &increase_call_exit_code,
      .main_action_cb = &gc_action_self_kill,
   };

   return test_sig(
      &generic_child,
      &ctx,
      0,
      43,
      0
   );
}

/* Test that with sigprocmask() a terminating signal still be masked */
int cmd_sig8(int argc, char **argv)
{
   struct generic_child_ctx ctx = {
      .sig = SIGTERM,
      .handler = SIG_DFL, /* default action: terminate for SIGTERM */
      .masked_sig1 = SIGTERM,
      .masked_sig2 = 0,
      .main_action_cb = &gc_action_self_kill,
   };

   return test_sig(
      &generic_child,
      &ctx,
      0,
      0,
      0
   );
}

/* Test that with sigprocmask() a SIGSEGV signal will still be masked */
int cmd_sigsegv3(int argc, char **argv)
{
   struct generic_child_ctx ctx = {
      .sig = SIGSEGV,
      .handler = SIG_DFL, /* default action: terminate for SIGSEGV */
      .masked_sig1 = SIGSEGV,
      .masked_sig2 = 0,
      .main_action_cb = &gc_action_self_kill,
   };

   return test_sig(
      &generic_child,
      &ctx,
      0,
      0,
      0
   );
}

/* Test that with sigprocmask() we cannot mask SIGKILL */
int cmd_sig9(int argc, char **argv)
{
   struct generic_child_ctx ctx = {
      .sig = SIGKILL,
      .handler = SIG_DFL,
      .masked_sig1 = SIGKILL,
      .masked_sig2 = 0,
      .main_action_cb = &gc_action_pause,
   };

   return test_sig(
      &generic_child,
      &ctx,
      SIGKILL,
      0,
      SIGKILL
   );
}

static bool is_single_signal_pending(int sig)
{
   sigset_t set;
   int rc;

   sigemptyset(&set);
   sigaddset(&set, sig);

   rc = sigpending(&set);

   if (rc != 0) {
      printf("FAIL: sigpending() failed with %s (%d)\n",
             strerror(errno), errno);
      exit(1);
   }

   return sigismember(&set, sig);
}

static void forking_sig_handler(int sig)
{
   int code, term_sig;
   int child_pid;
   int wstatus;
   int rc;

   DEVSHELL_CMD_ASSERT(sig == SIGUSR1);
   printf("child: send SIGUSR2 to myself, knowing that it is masked\n");

   rc = kill(getpid(), SIGUSR2);
   DEVSHELL_CMD_ASSERT(rc == 0);

   if (!is_single_signal_pending(SIGUSR2)) {
      printf("FAIL: SIGUSR2 is not pending in child\n");
      exit(1);
   }

   child_pid = fork();

   if (child_pid < 0) {
      printf("FAIL: fork() in sig handler failed with %s (%d)\n",
             strerror(errno), errno);
      exit(1);
   }

   if (!child_pid) {

      /* Mask SIGUSR1 because we know that our parent will send that */
      mask_signal(SIGUSR1);
      printf("** grandchild forked from signal handler, runs **\n");

      if (is_single_signal_pending(SIGUSR2)) {
         printf("FAIL: SIGUSR2 is pending in grandchild\n");
         exit(1);
      }

      /* Make sure to wait FOR THAN ENOUGH for SIGUSR1 to come */
      usleep(100 * 1000);

      /* Check that it is pending */
      if (!is_single_signal_pending(SIGUSR1)) {
         printf("FAIL: grandchild: SIGUSR1 is not pending\n");
         exit(1);
      }

      exit(42);
   }

   printf("child inside signal handler: sleep 10ms\n");
   usleep(50 * 1000);

   printf("child inside signal handler: send SIGUSR1 to grandchild\n");
   kill(child_pid, SIGUSR1);

   rc = waitpid(child_pid, &wstatus, 0);

   if (rc != child_pid) {
      printf("child inside signal handler: waitpid() returned %d instead of "
             "child's pid: %d\n", rc, child_pid);
      exit(1);
   }

   code = WEXITSTATUS(wstatus);
   term_sig = WTERMSIG(wstatus);

   printf("child inside signal handler: gradchild exit code: %d, sig: %d\n",
          code, term_sig);

   if (code != 42) {
      printf("FAIL: expected exit code == 42, got: %d\n", code);
      exit(1);
   }
}

/* Test that we can call fork() in a signal handler */
int cmd_sig10(int argc, char **argv)
{
   struct generic_child_ctx ctx = {
      .sig = SIGUSR1,
      .handler = &forking_sig_handler,
      .masked_sig1 = SIGUSR2,
      .masked_sig2 = 0,
      .main_action_cb = &gc_action_pause,
   };

   return test_sig(
      &generic_child,
      &ctx,
      0,
      0,
      SIGUSR1
   );
}


static void execve_sig_handler(int sig)
{
   int code, term_sig;
   int child_pid;
   int wstatus;
   int rc;

   DEVSHELL_CMD_ASSERT(sig == SIGUSR1);

   child_pid = fork();

   if (child_pid < 0) {
      printf("FAIL: fork() in sig handler failed with %s (%d)\n",
             strerror(errno), errno);
      exit(1);
   }

   if (!child_pid) {

      printf("grandchild: send SIGUSR2 to myself, knowing that it is masked\n");

      rc = kill(getpid(), SIGUSR2);
      DEVSHELL_CMD_ASSERT(rc == 0);

      if (!is_single_signal_pending(SIGUSR2)) {
         printf("FAIL: SIGUSR2 is not pending in grandchild\n");
         exit(1);
      }

      printf("grandchild: execute devshell, with SIGUSR2 pending\n");

      execle(get_devshell_path(), "devshell", "--blah", NULL, shell_env);

      /* We should never get here */
      printf("grandchild: execl failed with: %s (%d)\n",
             strerror(errno), errno);
      exit(1);
   }

   rc = waitpid(child_pid, &wstatus, 0);

   if (rc != child_pid) {
      printf("child inside signal handler: waitpid() returned %d instead of "
             "child's pid: %d\n", rc, child_pid);
      exit(1);
   }

   code = WEXITSTATUS(wstatus);
   term_sig = WTERMSIG(wstatus);

   printf("child inside signal handler: gradchild exit code: %d, sig: %d\n",
          code, term_sig);

   if (term_sig != SIGUSR2) {

      printf("FAIL: expected granchild to die with SIGUSR2, got instead: %d\n",
             term_sig);

      exit(1);
   }

   printf("child inside signal handler: "
          "the gradchild was killed by SIGUSR2, as expected\n");
}

/* Test that we can call fork() in a signal handler */
int cmd_sig11(int argc, char **argv)
{
   struct generic_child_ctx ctx = {
      .sig = SIGUSR1,
      .handler = &execve_sig_handler,
      .masked_sig1 = SIGUSR2,
      .masked_sig2 = 0,
      .main_action_cb = &gc_action_pause,
   };

   return test_sig(
      &generic_child,
      &ctx,
      0,
      0,
      SIGUSR1
   );
}

/* Test sigsuspend() */
int cmd_sig12(int argc, char **argv)
{
   sigset_t set, saved_mask;
   int rc;

   printf("sigsuspend() test: setup the initial mask\n");
   sigemptyset(&saved_mask);
   sigaddset(&saved_mask, SIGINT);
   sigaddset(&saved_mask, SIGQUIT);
   sigaddset(&saved_mask, SIGPWR);

   rc = sigprocmask(SIG_SETMASK, &saved_mask, NULL);

   if (rc) {
      printf("sigprocmask() failed with %s (%d)\n", strerror(errno), errno);
      exit(1);
   }

   printf("mask SIGUSR1 and SIGUSR2\n");
   mask_signal(SIGUSR1);
   mask_signal(SIGUSR2);
   memset((void *)test_got_sig, 0, sizeof(test_got_sig));

   signal(SIGUSR1, &child_sig_handler);
   signal(SIGUSR2, &child_sig_handler);

   printf("send both SIGUSR1 and SIGUSR2 to myself\n");
   kill(getpid(), SIGUSR1);
   kill(getpid(), SIGUSR2);

   printf("prepare the new mask which blocks everything but SIGUSR1\n");
   sigfillset(&set);
   sigdelset(&set, SIGUSR1);

   DEVSHELL_CMD_ASSERT(test_got_sig[SIGUSR1] == false);

   /* Call sigsupend and expect SIGUSR1 to be delievered */
   printf("call sigsuspend()\n");
   sigsuspend(&set);

   DEVSHELL_CMD_ASSERT(test_got_sig[SIGUSR1] == true);
   DEVSHELL_CMD_ASSERT(test_got_sig[SIGUSR2] == false);

   printf("unmask SIGUSR2\n");
   unmask_signal(SIGUSR2);
   DEVSHELL_CMD_ASSERT(test_got_sig[SIGUSR2] == true);

   unmask_signal(SIGUSR1);
   printf("retrive the original mask, expecting no change.\n");
   rc = sigprocmask(0, NULL, &set);

  if (rc) {
      printf("sigprocmask() failed with %s (%d)\n", strerror(errno), errno);
      exit(1);
   }

   for (int i = 1; i < _NSIG; i++) {

      if (sigismember(&set, i) != sigismember(&saved_mask, i)) {

         printf("FAIL: the current and saved mask differ\n");
         printf("current mask[%d]: %d\n", i, sigismember(&set, i));
         printf("saved mask[%d]:   %d\n", i, sigismember(&saved_mask, i));
      }
   }

   return 0;
}

static volatile int sig_chld_count;

static void
handle_sigchld(int sig)
{
   printf("parent: got SIGCHLD, count: %d\n", ++sig_chld_count);
}

/* Test that SIGCHLD is sent when a child changes its state */
int cmd_sig13(int argc, char **argv)
{
   int child_pid;
   int wstatus;
   int rc;

   sig_chld_count = 0;
   child_pid = fork();

   if (child_pid < 0) {
      printf("parent: fork() failed with %s (%d)\n", strerror(errno), errno);
      exit(1);
   }

   if (!child_pid) {

      printf("child: hello!\n");
      printf("child: waiting a little...\n");
      usleep(300 * 1000);
      printf("child: done\n");
      usleep(50 * 1000);
      printf("child: exit(42)\n");
      exit(42);
   }

   signal(SIGCHLD, &handle_sigchld);

   printf("parent: wait...\n");
   usleep(50 * 1000);
   printf("parent: send SIGSTOP..\n");
   kill(child_pid, SIGSTOP);
   printf("parent: wait...\n");
   usleep(50 * 1000);
   printf("parent: send SIGCONT..\n");
   kill(child_pid, SIGCONT);
   printf("parent: wait...\n");
   usleep(200 * 1000);
   printf("parent: waitpid()\n");

   do {
      rc = waitpid(child_pid, &wstatus, 0);
   } while (rc < 0 && errno == EINTR);

   DEVSHELL_CMD_ASSERT(rc == child_pid);
   DEVSHELL_CMD_ASSERT(WEXITSTATUS(wstatus) == 42);
   DEVSHELL_CMD_ASSERT(WTERMSIG(wstatus) == 0);
   DEVSHELL_CMD_ASSERT(sig_chld_count == 3);
   return 0;
}

/* Test that we can handle SIGSEGV, triggered by GPF */
int cmd_sigsegv4(int argc, char **argv)
{
   struct generic_child_ctx ctx = {
      .sig = SIGSEGV,
      .handler = &sig_handler_call_exit,
      .masked_sig1 = SIGSEGV, /* mask the signal: that should have no effect */
      .masked_sig2 = 0,
      .main_action_cb = &gc_action_gen_gpf,
   };

   return test_sig(
      &generic_child,
      &ctx,
      0,
      42,
      0
   );
}

/* Test that we can handle SIGSEGV, triggered by page fault */
int cmd_sigsegv5(int argc, char **argv)
{
   struct generic_child_ctx ctx = {
      .sig = SIGSEGV,
      .handler = &sig_handler_call_exit,
      .masked_sig1 = SIGSEGV, /* mask the signal: that should have no effect */
      .masked_sig2 = 0,
      .main_action_cb = &gc_action_gen_page_fault,
   };

   return test_sig(
      &generic_child,
      &ctx,
      0,
      42,
      0
   );
}
