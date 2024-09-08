/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <pwd.h>
#include <libgen.h>

#include "devshell.h"

bool dump_coverage;
char **shell_argv;
char **shell_env;

static char cmd_arg_buffers[MAX_ARGS][256];
static char *cmd_argv[MAX_ARGS];

static const char *devshell_path[] =
{
   "/bin/",
   "/usr/bin/",
   "/initrd/usr/bin/"
};

static bool contains_slash(const char *s)
{
   for (; *s; s++) {
      if (*s == '/')
         return true;
   }

   return false;
}

static void shell_builtin_cd(int argc)
{
   const char *dest_dir = "/";

   if (argc == 2 && strlen(cmd_argv[1]))
      dest_dir = cmd_argv[1];

   if (argc > 2) {
      fprintf(stderr, PFX "cd: too many arguments\n");
      return;
   }

   if (chdir(dest_dir)) {
      fprintf(stderr,
              PFX "cd: can't cd to '%s': %s\n",
              dest_dir,
              strerror(errno));
   }
}

static bool is_file(const char *filepath)
{
   struct stat statbuf;

   if (stat(filepath, &statbuf) < 0)
      return false;

   return (statbuf.st_mode & S_IFMT) == S_IFREG;
}

static void wait_child_cmd(int child_pid)
{
   int wstatus;
   waitpid(child_pid, &wstatus, 0);

   if (!WIFEXITED(wstatus)) {

      int term_sig = WTERMSIG(wstatus);
      printf("\n");

      if (term_sig != SIGINT)
         printf(PFX "Command terminated by signal: %d (%s)\n",
                term_sig, strsignal(term_sig));

      return;
   }

   if (WEXITSTATUS(wstatus))
      printf(PFX "Command exited with status: %d\n", WEXITSTATUS(wstatus));
}

static void shell_run_child(int argc)
{
   char buf[MAX_PATH];
   int saved_errno;
   int i;

   run_if_known_command(cmd_argv[0], argc - 1, cmd_argv + 1);

   /* Since we got here, cmd_argv[0] was NOT a known built-in command */

   if (!contains_slash(cmd_argv[0])) {

      if (argc > MAX_ARGS) {
         fprintf(stderr, PFX "Too many arguments. Limit: %d\n", MAX_ARGS);
         exit(1);
      }

      for (i = 0; i < ARRAY_SIZE(devshell_path); i++) {

         strcpy(buf, devshell_path[i]);
         strcat(buf, cmd_argv[0]);

         if (is_file(buf))
            break;
      }

      if (i == ARRAY_SIZE(devshell_path)) {
         fprintf(stderr, PFX "Command '%s' not found\n", cmd_argv[0]);
         exit(1);
      }

      cmd_argv[0] = buf;
   }

   execve(cmd_argv[0], cmd_argv, shell_env);

   /* if we got here, execve() failed */
   saved_errno = errno;
   perror(cmd_argv[0]);
   exit(saved_errno);
}

static int parse_cmd_line(const char *cmd_line)
{
   int argc = 0;
   char quote_char;
   char *arg = NULL;
   bool in_arg = false;
   bool in_quotes = false;

   for (const char *p = cmd_line; *p && *p != '\n'; p++) {

      if (!in_arg) {

         if (*p == ' ')
            continue;

         if (argc == MAX_ARGS)
            break;

         in_arg = true;
         cmd_argv[argc] = cmd_arg_buffers[argc];
         arg = cmd_argv[argc];
         argc++;
      }

      if (!in_quotes) {

         if (*p == ' ') {
            in_arg = false;
            *arg = 0;
            continue;
         }

         if (*p == '\'' || *p == '"') {
            in_quotes = true;
            quote_char = *p;
            continue;
         }

      } else {

         if (*p == quote_char) {
            in_quotes = false;
            continue;
         }
      }

      *arg++ = *p;
   }

   if (in_arg)
      *arg = 0;

   cmd_argv[argc] = NULL;

   if (in_quotes) {
      fprintf(stderr, PFX "ERROR: Unterminated quote %c\n", quote_char);
      return 0;
   }

   return argc;
}

static void process_cmd_line(const char *cmd_line)
{
   int child_pid;
   int argc = parse_cmd_line(cmd_line);

   if (!argc || !cmd_argv[0][0])
      return;

   if (!strcmp(cmd_argv[0], "cd")) {
      shell_builtin_cd(argc);
      return;
   }

   if (!strcmp(cmd_argv[0], "exit")) {
      exit(0);
   }

   if ((child_pid = fork()) < 0) {
      perror("fork failed");
      return;
   }

   if (!child_pid) {
      shell_run_child(argc);
   }

   wait_child_cmd(child_pid);
}

static void show_help_and_exit(void)
{
   show_common_help_intro();

   printf(COLOR_RED "Usage:" RESET_ATTRS "\n\n");
   printf("    devshell %-15s Just run the interactive shell\n", " ");
   printf("    devshell %-15s Show this help and exit\n\n", "-h, --help");

   printf("    Internal test-infrastructure options\n");
   printf("    ------------------------------------\n\n");
   printf("    devshell %-15s List the built-in (test) commands\n", "-l");
   printf("    devshell %-15s Just dump the kernel coverage data\n\n", "-dcov");

   printf("    devshell [-dcov] -c <cmd> [arg1 [arg2 [arg3...]]]\n");
   printf("%-28s Run the <cmd> built-in command and exit.\n", " ");
   printf("%-28s In case -c is preceded by -dcov, the devshell\n", " ");
   printf("%-28s also dumps the kernel coverage data on-screen.\n", " ");
   exit(0);
}

static void parse_opt(int argc, char **argv)
{
   for (; argc > 0; argc--, argv++) {

      if (!strlen(*argv))
         continue;

      if (!strcmp(*argv, "-h") || !strcmp(*argv, "--help"))
         show_help_and_exit();

      if (!strcmp(*argv, "-l"))
         dump_list_of_commands_and_exit();

      if (!strcmp(*argv, "-dcov")) {

         if (argc > 1) {
            dump_coverage = true;
            continue;
         }

         dump_coverage_files();
         exit(0);
      }

      if (argc == 1)
         goto unknown_opt;

      /* argc > 1 */

      if (!strcmp(*argv, "-c")) {
         printf(PFX "Executing built-in command '%s'\n", argv[1]);
         run_if_known_command(argv[1], argc - 2, argv + 2);
         printf(PFX "Unknown built-in command '%s'\n", argv[1]);
         return;
      }

   unknown_opt:
      printf(PFX "Unknown option '%s'\n", *argv);
      break;
   }
}

const char *get_devshell_path(void)
{
   if (!getenv("TILCK")) {

      /*
       * When running this test on Linux, we cannot expect to find the devshell
       * in the same abs path (/initrd/...) as on Tilck.
       */

      return "/proc/self/exe";
   }

   return DEVSHELL_PATH;
}

static void setup_signals(void)
{
   sigset_t set;
   int rc;

   signal(SIGINT, SIG_IGN);
   signal(SIGQUIT, SIG_IGN);

   sigemptyset(&set);
   rc = sigprocmask(SIG_SETMASK, &set, NULL);

   if (rc) {

      fprintf(stderr,
              "devshell: sigprocmask() failed with: %s (%d)\n",
              strerror(errno), errno);

      exit(1);
   }
}

int main(int argc, char **argv, char **env)
{
   static char cmdline_buf[256];
   static char cwd_buf[256];
   static struct termios orig_termios;
   const char *cmdname;
   char uc = '#';
   int rc;

   setup_signals();

   /* Save current term's mode */
   tcgetattr(0, &orig_termios);

   shell_argv = argv;
   shell_env = env;

   if (!argc) {
      fprintf(stderr, PFX "Weird error: argc == 0\n");
      return 1;
   }

   strncpy(cwd_buf, argv[0], sizeof(cwd_buf) - 1);
   cmdname = basename(cwd_buf);

   if (strcmp(cmdname, "devshell")) {
      printf(PFX "Executing built-in command '%s'\n", cmdname);
      run_if_known_command(cmdname, argc - 1, argv + 1);
      printf(PFX "Unknown built-in command '%s'\n", cmdname);
      return 1;
   }

   if (argc > 1) {
      parse_opt(argc - 1, argv + 1);
      exit(1);
   }

   /* No command specified in the options: run in interactive mode */
   if (getuid() != 0)
      uc = '$';

   while (true) {

      if (getcwd(cwd_buf, sizeof(cwd_buf)) != cwd_buf) {
         fprintf(stderr, PFX "getcwd() failed: %s", strerror(errno));
         return 1;
      }

      printf(COLOR_RED "[TilckDevShell]" RESET_ATTRS ":%s%c ", cwd_buf, uc);
      fflush(stdout);

      rc = read_command(cmdline_buf, sizeof(cmdline_buf));

      if (rc < 0) {
         fprintf(stderr, PFX "I/O error\n");
         break;
      }

      if (rc)
         process_cmd_line(cmdline_buf);

      /* Restore term's mode, in case the command corrupted it */
      tcsetattr(0, TCSAFLUSH, &orig_termios);
   }

   return 0;
}
