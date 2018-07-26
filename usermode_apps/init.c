
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
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


int main(int argc, char **argv, char **env)
{
   int shell_pid;
   int wstatus;

   if (getenv("TILCK")) {

      open_std_handles();

      if (getpid() != 1) {
         printf("[init] ERROR: my pid is %i instead of 1\n", getpid());
         call_exit(1);
      }
   }

   if (argc > 1 && !strcmp(argv[1], "--")) {
      for (int i = 1; i < ARRAY_SIZE(shell_args)-1 && i + 1 < argc; i++) {
         shell_args[i] = argv[i + 1];
      }
   }

   shell_pid = fork();

   if (!shell_pid) {
      execve("/bin/devshell", shell_args, NULL);
   }

   waitpid(shell_pid, &wstatus, 0);
   printf("[init] the devshell exited with status: %d\n", WEXITSTATUS(wstatus));
   call_exit(0);
}

