/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <fcntl.h>
#include <stdbool.h>
#include <errno.h>

#define ARRAY_SIZE(x) ((int)sizeof(x)/(int)sizeof(x[0]))

void call_exit(int code)
{
   printf("[init] exit with code: %i\n", code);
   exit(code);
}

void open_std_handles(void)
{
   int in = open("/dev/tty", O_RDONLY);
   int out = open("/dev/tty", O_WRONLY);
   int err = open("/dev/tty", O_WRONLY);

   if (in != 0) {
      printf("[init] in: %i, expected: 0\n", in);
      call_exit(1);
   }

   if (out != 1) {
      printf("[init] out: %i, expected: 1\n", out);
      call_exit(1);
   }

   if (err != 2) {
      printf("[init] err: %i, expected: 2\n", err);
      call_exit(1);
   }
}

char *shell_args[16] = { "/bin/devshell", [1 ... 15] = NULL };

static void report_shell_exit(int wstatus)
{
   const int status = WEXITSTATUS(wstatus);
   printf("[init] the devshell exited with status: %d\n", status);

   /*
    * HACK: exit as soon as the shell exits, instead of waiting all the
    * eventual other processes to exit as well.
    *
    * TODO: remove this.
    */
   call_exit(0);
}

static void report_process_exit(pid_t pid, int wstatus)
{
   const int status = WEXITSTATUS(wstatus);
   printf("[init] process %d exited with status: %d\n", pid, status);
}

int main(int argc, char **argv, char **env)
{
   int shell_pid;
   int wstatus;
   pid_t pid;

   if (getenv("TILCK")) {

      open_std_handles();

      if (getpid() != 1) {
         printf("[init] ERROR: my pid is %i instead of 1\n", getpid());
         call_exit(1);
      }

   } else {

      if (prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) < 0)
         perror("prctl(PR_SET_CHILD_SUBREAPER) failed");
   }

   if (argc > 1 && !strcmp(argv[1], "--")) {
      for (int i = 0; i < ARRAY_SIZE(shell_args) && i + 2 < argc; i++) {
         shell_args[i] = argv[i + 2];
      }
   }

   shell_pid = fork();

   if (!shell_pid) {
      if (execve(shell_args[0], shell_args, NULL) < 0) {
         perror("execve failed");
         call_exit(1);
      }
   }

   do {

      pid = waitpid(-1, &wstatus, 0);

      if (pid == shell_pid)
         report_shell_exit(wstatus);
      else if (pid > 0)
         report_process_exit(pid, wstatus);

   } while (pid > 0);

   call_exit(0);
}

