/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/wait.h>

#include "devshell.h"
#include "sysenter.h"

#define DECL_CMD(name) int cmd_##name(int argc, char **argv)

DECL_CMD(help);
DECL_CMD(selftest);
DECL_CMD(runall);
DECL_CMD(loop);
DECL_CMD(fork);
DECL_CMD(sysenter);
DECL_CMD(fork_se);
DECL_CMD(bad_read);
DECL_CMD(bad_write);
DECL_CMD(fork_perf);
DECL_CMD(syscall_perf);
DECL_CMD(fpu);
DECL_CMD(fpu_loop);
DECL_CMD(brk);
DECL_CMD(mmap);
DECL_CMD(kcow);
DECL_CMD(wpid1);
DECL_CMD(wpid2);
DECL_CMD(wpid3);
DECL_CMD(wpid4);
DECL_CMD(sigsegv1);
DECL_CMD(sigsegv2);
DECL_CMD(sigill);
DECL_CMD(sigfpe);
DECL_CMD(sigabrt);
DECL_CMD(sig1);
DECL_CMD(select1);
DECL_CMD(select2);
DECL_CMD(poll1);
DECL_CMD(poll2);
DECL_CMD(poll3);

int cmd_bigargv(int argc, char **argv)
{
   int rc;
   int pid;
   int wstatus;

   pid = fork();

   if (pid < 0) {
      perror("fork() failed");
      return 1;
   }

   if (!pid) {

      size_t len = 4051; //4052;
      printf("len: %d\n", (int)len);

      char *big_arg = malloc(len + 1);
      memset(big_arg, 'a', len);
      big_arg[len] = 0;

      char *argv[3] = { "/bin/devshell", big_arg, NULL };
      char *env[] = { NULL };

      printf("before execve()...\n");
      rc = execve(argv[0], argv, env);

      if (rc)
         perror("execve failed");

      exit(0);
   }

   waitpid(pid, &wstatus, 0);
   return 0;
}

int cmd_selftest(int argc, char **argv)
{
   if (argc < 1) {
      printf("[shell] Expected selftest name argument.\n");
      return 1;
   }

   int rc =
      sysenter_call3(TILCK_TESTCMD_SYSCALL,
                     TILCK_TESTCMD_RUN_SELFTEST,
                     argv[0]  /* self test name */,
                     NULL);

   if (rc != 0) {
      printf("[shell] Invalid selftest '%s'\n", argv[0]);
      return 1;
   }

   return 0;
}

/* ------------------------------------------- */

enum timeout_type {
   TT_SHORT = 0,
   TT_MED   = 1,
   TT_LONG  = 2,
};

static const char *tt_str[] =
{
   [TT_SHORT] = "tt_short",
   [TT_MED] = "tt_med",
   [TT_LONG] = "tt_long"
};

#define CMD_ENTRY(name, len, enabled) {#name, cmd_ ## name, len, enabled}

struct {

   const char *name;
   cmd_func_type func;
   enum timeout_type tt;
   bool enabled_in_st;

} cmds_table[] = {

   CMD_ENTRY(help, TT_SHORT, false),
   CMD_ENTRY(selftest, TT_LONG, false),
   CMD_ENTRY(runall, TT_LONG, false),
   CMD_ENTRY(loop, TT_MED, false),
   CMD_ENTRY(fork, TT_MED, true),
   CMD_ENTRY(sysenter, TT_SHORT, true),
   CMD_ENTRY(fork_se, TT_MED, true),
   CMD_ENTRY(bad_read, TT_SHORT, true),
   CMD_ENTRY(bad_write, TT_SHORT, true),
   CMD_ENTRY(fork_perf, TT_LONG, true),
   CMD_ENTRY(syscall_perf, TT_SHORT, true),
   CMD_ENTRY(fpu, TT_SHORT, true),
   CMD_ENTRY(fpu_loop, TT_LONG, false),
   CMD_ENTRY(brk, TT_SHORT, true),
   CMD_ENTRY(mmap, TT_MED, true),
   CMD_ENTRY(kcow, TT_SHORT, true),
   CMD_ENTRY(wpid1, TT_SHORT, true),
   CMD_ENTRY(wpid2, TT_SHORT, true),
   CMD_ENTRY(wpid3, TT_SHORT, true),
   CMD_ENTRY(wpid4, TT_SHORT, true),
   CMD_ENTRY(sigsegv1, TT_SHORT, true),
   CMD_ENTRY(sigsegv2, TT_SHORT, true),
   CMD_ENTRY(sigill, TT_SHORT, true),
   CMD_ENTRY(sigfpe, TT_SHORT, true),
   CMD_ENTRY(sigabrt, TT_SHORT, true),
   CMD_ENTRY(sig1, TT_SHORT, true),

   CMD_ENTRY(bigargv, TT_SHORT, false),

   CMD_ENTRY(select1, TT_SHORT, false),
   CMD_ENTRY(select2, TT_SHORT, false),
   CMD_ENTRY(poll1, TT_SHORT, false),
   CMD_ENTRY(poll2, TT_SHORT, false),
   CMD_ENTRY(poll3, TT_SHORT, false)
};

#undef CMD_ENTRY

static bool
run_child(int argc, char **argv, cmd_func_type func, const char *name)
{
   static const char *pass_fail_strings[2] = {
      COLOR_RED "[FAILED] " RESET_ATTRS,
      COLOR_GREEN "[PASSED] " RESET_ATTRS,
   };

   int child_pid, wstatus;
   u64 start_ms, end_ms;
   struct timeval tv;
   bool pass;

   gettimeofday(&tv, NULL);
   start_ms = (u64)tv.tv_sec * 1000 + (u64)tv.tv_usec / 1000;

   printf(COLOR_YELLOW "[devshell] ");
   printf(COLOR_GREEN "[RUN   ] " RESET_ATTRS "%s"  "\n", name);

   child_pid = fork();

   if (child_pid < -1) {
      perror("fork failed");
      exit(1);
   }

   if (!child_pid) {
      exit(func(argc, argv));
   }

   waitpid(child_pid, &wstatus, 0);

   gettimeofday(&tv, NULL);
   end_ms = (u64)tv.tv_sec * 1000 + (u64)tv.tv_usec / 1000;

   pass = WIFEXITED(wstatus) && (WEXITSTATUS(wstatus) == 0);
   printf(COLOR_YELLOW "[devshell] %s", pass_fail_strings[pass]);
   printf("%s (%llu ms)\n\n", name, end_ms - start_ms);
   return pass;
}

int cmd_runall(int argc, char **argv)
{
   bool any_failure = false;
   int wstatus;
   int child_pid;
   int to_run = 0, passed = 0;
   u64 start_ms, end_ms;
   struct timeval tv;

   gettimeofday(&tv, NULL);
   start_ms = (u64)tv.tv_sec * 1000 + (u64)tv.tv_usec / 1000;

   for (int i = 1; i < ARRAY_SIZE(cmds_table); i++) {

      if (!cmds_table[i].enabled_in_st || cmds_table[i].tt == TT_LONG)
         continue;

      to_run++;

      if (!run_child(argc, argv, cmds_table[i].func, cmds_table[i].name)) {
         any_failure = true;
         continue;
      }

      passed++;
   }

   gettimeofday(&tv, NULL);
   end_ms = (u64)tv.tv_sec * 1000 + (u64)tv.tv_usec / 1000;

   printf(COLOR_YELLOW "[devshell] ");
   printf("------------------------------------------------------------\n");
   printf(COLOR_YELLOW "[devshell] ");

   printf(passed == to_run ? COLOR_GREEN : COLOR_RED);
   printf("Tests passed %d/%d" RESET_ATTRS " ", to_run, passed);
   printf("(%llu ms)\n\n", end_ms - start_ms);

   if (dump_coverage) {
      dump_coverage_files();
   }

   exit(any_failure);
}

void dump_list_of_commands(void)
{
   for (int i = 1; i < ARRAY_SIZE(cmds_table); i++) {
      if (cmds_table[i].enabled_in_st)
         printf("%s %s\n", cmds_table[i].name, tt_str[cmds_table[i].tt]);
   }

   exit(0);
}

int cmd_help(int argc, char **argv)
{
   size_t n, row_len;
   char buf[64];

   printf("\n");
   printf(COLOR_RED "Tilck development shell\n" RESET_ATTRS);

   printf("This application is a small dev-only utility written in ");
   printf("order to allow running\nsimple programs, while proper shells ");
   printf("like ASH can't run on Tilck yet. Behavior:\nif a given command ");
   printf("isn't an executable (e.g. /bin/termtest), it is forwarded ");
   printf("to\n" COLOR_YELLOW "/bin/busybox" RESET_ATTRS);
   printf(". That's how several programs like 'ls' work. Type --help to see\n");
   printf("all the commands built in busybox.\n\n");

   printf(COLOR_RED "Built-in commands\n" RESET_ATTRS);
   printf("    help         shows this help\n");
   printf("    cd <DIR>     change the current working directory\n");
   printf("    runall       run all the shellcmd tests\n\n");
   printf(COLOR_RED "Kernel test commands\n" RESET_ATTRS);

   row_len = printf("    ");

   for (int i = 1 /* skip help */; i < ARRAY_SIZE(cmds_table); i++) {

      n = sprintf(buf, "%s%s", cmds_table[i].name,
                  i != ARRAY_SIZE(cmds_table)-1 ? "," : "");

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

static void run_cmd(cmd_func_type func, int argc, char **argv)
{
   int exit_code = func(argc, argv);

   if (dump_coverage) {
      dump_coverage_files();
   }

   exit(exit_code);
}

void run_if_known_command(const char *cmd, int argc, char **argv)
{
   for (int i = 0; i < ARRAY_SIZE(cmds_table); i++)
      if (!strcmp(cmds_table[i].name, cmd))
         run_cmd(cmds_table[i].func, argc, argv);
}
