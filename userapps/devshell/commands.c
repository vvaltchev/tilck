/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>

#include "devshell.h"
#include "sysenter.h"

extern struct test_cmd_entry *cmds_table;

static const char *tt_str[] =
{
   [TT_SHORT] = "tt_short",
   [TT_MED] = "tt_med",
   [TT_LONG] = "tt_long",
};

static int get_cmds_count(void)
{

   static int value;
   struct test_cmd_entry *e;

   if (value)
      return value;

   for (e = cmds_table; e->name != NULL; e++) {
      value++;
   }

   return value;
}

int cmd_selftest(int argc, char **argv)
{
   int rc;

   if (argc < 1) {
      fprintf(stderr, PFX "Expected a <selftest> argument.\n");
      return 1;
   }

   rc = sysenter_call3(TILCK_CMD_SYSCALL,
                       TILCK_CMD_RUN_SELFTEST,
                       argv[0], /* self test name */
                       NULL);

   if (rc != 0) {
      fprintf(stderr, PFX "Invalid selftest '%s'\n", argv[0]);
      return 1;
   }

   return 0;
}

static u64 get_monotonic_time_ms(void)
{
   int rc;
   struct timespec tp;

   rc = clock_gettime(CLOCK_MONOTONIC, &tp);

   if (rc < 0) {
      perror("clock_gettime(CLOCK_MONOTONIC) failed");
      exit(1);
   }

   return (u64)tp.tv_sec * 1000 + (u64)tp.tv_nsec / 1000000;
}

/*
 * Per-test wall-clock budget. Matches the Python test runner's
 * per-test timeouts in tests/runners/run_all_tests so devshell's
 * own watchdog races the runner's; whichever fires first wins,
 * and devshell's cleaner failure path -- "TIMEOUT: '<name>' ...,
 * killing process group" + continue with the next test -- beats
 * the runner-level "VM timed out" outcome.
 */
static int test_timeout_ms(enum timeout_type tt)
{
   switch (tt) {
      case TT_SHORT: return  12 * 1000;
      case TT_MED:   return  36 * 1000;
      case TT_LONG:  return 108 * 1000;
   }
   return 36 * 1000;
}

static bool
run_child(int argc,
          char **argv,
          cmd_func_type func,
          const char *name,
          enum timeout_type tt)
{
   static const char *pass_fail_strings[2] = {
      COLOR_RED STR_FAIL RESET_ATTRS,
      COLOR_GREEN STR_PASS RESET_ATTRS,
   };

   int child_pid, wstatus, rc;
   u64 start_ms, end_ms;
   int timeout_ms, waited_ms = 0;
   bool pass, timed_out = false;

   start_ms = get_monotonic_time_ms();

   printf(COLOR_YELLOW PFX);
   printf(COLOR_GREEN STR_RUN RESET_ATTRS "%s"  "\n", name);

   child_pid = fork();

   if (child_pid < -1) {
      perror("fork failed");
      exit(1);
   }

   if (!child_pid) {

      /*
       * Become leader of a new process group so the run_child
       * parent can SIGKILL the entire test process tree --
       * including any grandchildren the test itself fork()ed --
       * if the test hangs. Without this, devshell can only
       * waitpid() on its direct child; a grandchild stuck in an
       * indefinite wait (sigsuspend, pause, deadlocked pipe,
       * ...) would otherwise leak as an orphan and block runall.
       */
      setpgid(0, 0);
      exit(func(argc, argv));
   }

   /*
    * Race-close: also set the child's pgrp from the parent. If
    * the child hasn't yet reached its own setpgid() and we time
    * out and kill(-child_pid, ...) the group, this guarantees
    * the child is in the right group at that moment. Idempotent
    * if the child already did it; -ESRCH (child exited) is fine.
    */
   setpgid(child_pid, child_pid);

   timeout_ms = test_timeout_ms(tt);

   while (true) {

      rc = waitpid(child_pid, &wstatus, WNOHANG);

      if (rc < 0) {
         perror("waitpid() failed");
         return false;
      }

      if (rc == child_pid)
         break;   /* child exited and was reaped */

      /* rc == 0: child still alive */

      if (waited_ms >= timeout_ms) {
         timed_out = true;
         break;
      }

      usleep(100 * 1000);   /* poll every 100 ms */
      waited_ms += 100;
   }

   if (timed_out) {

      printf(COLOR_YELLOW PFX COLOR_RED "TIMEOUT: " RESET_ATTRS
             "'%s' didn't finish in %d ms; killing process group\n",
             name, timeout_ms);

      /*
       * kill(-pgid, SIGKILL): hit every process in the test's
       * process group. The child + any grandchildren it forked
       * die at once and the final waitpid below reaps the direct
       * child; grandchildren get reparented to init and reaped
       * there.
       */
      kill(-child_pid, SIGKILL);

      rc = waitpid(child_pid, &wstatus, 0);

      if (rc < 0) {
         perror("waitpid() after SIGKILL failed");
         return false;
      }
   }

   if (rc != child_pid) {
      fprintf(stderr, "waitpid() returned value [%d] "
                      "!= child_pid [%d]\n", rc, child_pid);
      return false;
   }

   end_ms = get_monotonic_time_ms();

   pass = !timed_out && WIFEXITED(wstatus) && (WEXITSTATUS(wstatus) == 0);
   printf(COLOR_YELLOW PFX "%s", pass_fail_strings[pass]);
   printf("%s (%" PRIu64 " ms)\n\n", name, end_ms - start_ms);
   return pass;
}

int cmd_runall(int argc, char **argv)
{
   u64 start_ms, end_ms;
   bool any_failure = false;
   int to_run = 0, passed = 0;

   start_ms = get_monotonic_time_ms();

   for (int i = 1; i < get_cmds_count(); i++) {

      if (!cmds_table[i].enabled_in_st || cmds_table[i].tt == TT_LONG)
         continue;

      to_run++;

      if (!run_child(argc, argv,
                     cmds_table[i].func,
                     cmds_table[i].name,
                     cmds_table[i].tt))
      {
         any_failure = true;
         continue;
      }

      passed++;
   }

   end_ms = get_monotonic_time_ms();

   printf(COLOR_YELLOW PFX);
   printf("------------------------------------------------------------\n");
   printf(COLOR_YELLOW PFX);

   printf(passed == to_run ? COLOR_GREEN : COLOR_RED);
   printf("Tests passed %d/%d" RESET_ATTRS " ", passed, to_run);
   printf("(%" PRIu64 " ms)\n\n", end_ms - start_ms);

   if (dump_coverage) {
      dump_coverage_files();
   }

   exit(any_failure);
}

void dump_list_of_commands_and_exit(void)
{
   for (int i = 1; i < get_cmds_count(); i++) {
      if (cmds_table[i].enabled_in_st)
         printf("%s %s\n", cmds_table[i].name, tt_str[cmds_table[i].tt]);
   }

   exit(0);
}

void show_common_help_intro(void)
{
   printf("\n");
   printf(COLOR_RED "Tilck development shell\n" RESET_ATTRS);

   printf("This application is a small dev-only utility originally written ");
   printf("in order to \nallow running simple programs, while proper shells ");
   printf("like ASH couldn't run on \nTilck yet. Today, " COLOR_YELLOW);
   printf("ASH works on Tilck" RESET_ATTRS " and this ");
   printf(COLOR_YELLOW "devshell" RESET_ATTRS " is used as a runner \n");
   printf("for system tests (often called 'shellcmds' for this reason).\n\n");
}

int cmd_help(int argc, char **argv)
{
   size_t n, row_len;
   char buf[64];

   show_common_help_intro();

   printf(COLOR_RED "Built-in commands\n" RESET_ATTRS);
   printf("    help         shows this help\n");
   printf("    cd <DIR>     change the current working directory\n");
   printf("    runall       run all the shellcmd tests\n\n");
   printf(COLOR_RED "Kernel test commands\n" RESET_ATTRS);

   row_len = printf("    ");

   for (int i = 1 /* skip help */; i < get_cmds_count(); i++) {

      n = sprintf(buf, "%s%s", cmds_table[i].name,
                  i != get_cmds_count()-1 ? "," : "");

      if (row_len + n >= 80) {
         printf("\n");
         row_len = printf("    ");
      } else if (i > 1) {
         row_len += printf(" ");
      }

      row_len += printf("%s", buf);
   }

   printf("\n\n");
   return 0;
}

void do_cmd_exit(int code)
{
   if (!dump_coverage && code != 0) {
      printf(COLOR_YELLOW PFX RESET_ATTRS);
      printf("The command %sFAILED%s with %s%d%s\n",
             COLOR_RED, RESET_ATTRS, COLOR_YELLOW, code, RESET_ATTRS);
   }

   exit(code);
}

static void run_cmd(cmd_func_type func, int argc, char **argv)
{
   int exit_code = func(argc, argv);

   if (dump_coverage)
      dump_coverage_files();

   do_cmd_exit(exit_code);
}

void run_if_known_command(const char *cmd, int argc, char **argv)
{
   /* Reset all the signal handlers to their default behavior */
   for (int i = 1; i < _NSIG; i++)
     signal(i, SIG_DFL);

   for (int i = 0; i < get_cmds_count(); i++)
      if (!strcmp(cmds_table[i].name, cmd))
         run_cmd(cmds_table[i].func, argc, argv);
}

/*
 * When the tests are compiled with optimizations like -Os or higher, some
 * memcpy() are optimized out (e.g. fmmap4 test) because the compiler see no
 * point in performing the memcpy (while we expect to get killed a signal).
 *
 * Therefore, we need a simple "forced" memcpy that will always do what we
 * expect, even with -O3.
 */

void
forced_memcpy(void *dest, const void *src, size_t n)
{
   for (size_t i = 0; i < n / 4; i++)
      ((volatile u32 *)dest)[i] = ((const volatile u32 *)src)[i];

   for (size_t i = 0; i < n % 4; i++)
      ((volatile u8 *)dest)[i] = ((const volatile u8 *)src)[i];
}
