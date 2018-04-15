
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>
#include <errno.h>

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

int main(int argc, char **argv, char **env)
{
   int shell_pid;
   int wstatus;

   if (getenv("EXOS"))
      open_std_handles();

   printf("[init] Hello from init! MY PID IS %i\n", getpid());

   shell_pid = fork();

   if (!shell_pid) {
      printf("[init forked child] running shell\n");
      execve("/bin/shell", NULL, NULL);
   }

   printf("[init] wait for the shell to exit\n");

   waitpid(shell_pid, &wstatus, 0);
   printf("[init] the shell exited with status: %d\n", WEXITSTATUS(wstatus));
   call_exit(0);
}

