/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <pwd.h>

#include "devshell.h"

bool dump_coverage;

static char cmd_arg_buffers[MAX_ARGS][256];
static char *cmd_argv[MAX_ARGS];
static char **shell_env;

static void shell_builtin_cd(int argc)
{
   int rc = 0;
   const char *dest_dir = "/";

   if (argc == 2 && strlen(cmd_argv[1])) {
      dest_dir = cmd_argv[1];
   }

   if (argc > 2) {
      printf("cd: too many arguments\n");
      return;
   }

   rc = chdir(dest_dir);

   if (rc < 0)
      goto cd_error;

   return;

cd_error:
   perror("cd");
   return;
}

static bool file_exists(const char *filepath)
{
   struct stat statbuf;
   int rc = stat(filepath, &statbuf);
   return !rc;
}

static void wait_child_cmd(int child_pid)
{
   int wstatus;
   waitpid(child_pid, &wstatus, 0);

   if (!WIFEXITED(wstatus)) {
      printf("[shell] the command did NOT exited normally\n");
      return;
   }

   if (WEXITSTATUS(wstatus))
      printf("[shell] command exited with status: %d\n", WEXITSTATUS(wstatus));
}

static void process_cmd_line(const char *cmd_line)
{
   int argc = 0;
   const char *p = cmd_line;

   while (*p && argc < MAX_ARGS) {

      char *ap = cmd_arg_buffers[argc];

      while (*p == ' ') p++;

      while (*p && *p != ' ' && *p != '\n') {
         *ap++ = *p++;
      }

      *ap = 0;
      cmd_argv[argc] = cmd_arg_buffers[argc];
      argc++;

      if (*p == '\n')
         break;
   }

   cmd_argv[argc] = NULL;

   if (!cmd_argv[0][0])
      return;

   if (!strcmp(cmd_argv[0], "cd")) {
      shell_builtin_cd(argc);
      return;
   }

   if (!strcmp(cmd_argv[0], "exit")) {
      printf("[shell] regular exit\n");
      exit(0);
   }

   // printf("[process_cmd_line] args(%i):\n", argc);
   // for (int i = 0; cmd_argv[i] != NULL; i++)
   //    printf("[process_cmd_line] argv[%i] = '%s'\n", i, cmd_argv[i]);

   int child_pid = fork();

   if (!child_pid) {

      run_if_known_command(cmd_argv[0], argc - 1, cmd_argv + 1);

      /* since we got here, cmd_argv[0] was NOT a known command */

      if (!file_exists(cmd_argv[0]) && argc < MAX_ARGS) {
         if (file_exists("/bin/busybox")) {

            for (int i = argc; i > 0; i--)
               cmd_argv[i] = cmd_argv[i - 1];

            cmd_argv[++argc] = NULL;
            cmd_argv[0] = "/bin/busybox";
         }
      }

      execve(cmd_argv[0], cmd_argv, NULL);
      int saved_errno = errno;
      perror(cmd_argv[0]);
      exit(saved_errno);
   }

   if (child_pid == -1) {
      perror("fork failed");
      return;
   }

   wait_child_cmd(child_pid);
}

static void show_help_and_exit(void)
{
   printf("\nUsage:\n\n");
   printf("    devshell %-15s Just run the interactive shell\n", " ");
   printf("    devshell %-15s Show this help and exit\n\n", "-h/--help");

   printf("    Internal test-infrastructure options\n");
   printf("    ------------------------------------\n\n");
   printf("    devshell %-15s List the built-in (test) commands\n\n", "-l");

   printf("    devshell [-dcov] -c <cmd> [arg1 [arg2 [arg3...]]]\n");
   printf("%-28s Run the <cmd> built-in command and exit.\n", " ");
   printf("%-28s In case -c is preceded by -dcov, the devshell\n", " ");
   printf("%-28s also dumps the kernel coverage data on-screen.\n", " ");
   exit(0);
}

static void parse_opt(int argc, char **argv)
{
begin:

   if (!argc)
      return;

   if (!strlen(*argv)) {
      argc--; argv++;
      goto begin;
   }

   if (!strcmp(*argv, "-h") || !strcmp(*argv, "--help")) {
      show_help_and_exit();
   }

   if (!strcmp(*argv, "-l")) {
      dump_list_of_commands();
      /* not reached */
   }

   if (argc == 1)
      goto unknown_opt;

   /* argc > 1 */

   if (!strcmp(*argv, "-dcov")) {
      dump_coverage = true;
      argc--; argv++;
      goto begin;
   }

   if (!strcmp(*argv, "-c")) {
      printf("[shell] Executing built-in command '%s'\n", argv[1]);
      run_if_known_command(argv[1], argc - 2, argv + 2);
      printf("[shell] Unknown built-in command '%s'\n", argv[1]);
      return;
   }

unknown_opt:
   printf("[shell] Unknown option '%s'\n", *argv);
}

int main(int argc, char **argv, char **env)
{
   static char cmdline_buf[256];
   static char cwd_buf[256];
   static struct utsname utsname_buf;

   int uid = geteuid();
   struct passwd *pwd = getpwuid(uid);

   shell_env = env;

   if (argc > 1) {
      parse_opt(argc - 1, argv + 1);
      exit(1);
   }

   if (!pwd) {
      printf("ERROR: getpwuid() returned NULL\n");
      return 1;
   }

   if (uname(&utsname_buf) < 0) {
      perror("uname() failed");
      return 1;
   }

   while (true) {

      if (getcwd(cwd_buf, sizeof(cwd_buf)) != cwd_buf) {
         perror("Shell: getcwd() failed");
         return 1;
      }

      printf("%s@%s:%s%c ",
             pwd->pw_name,
             utsname_buf.nodename,
             cwd_buf,
             !uid ? '#' : '$');

      fflush(stdout);

      int rc = read_command(cmdline_buf, sizeof(cmdline_buf));

      if (rc < 0) {
         printf("I/O error\n");
         break;
      }

      if (rc)
         process_cmd_line(cmdline_buf);
   }

   return 0;
}
