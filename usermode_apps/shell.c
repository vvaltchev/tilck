
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

char cmd_arg_buffers[16][256];
char *cmd_argv[16];
char **shell_env;

void process_cmd_line(const char *cmd_line)
{
   int argc = 0;
   const char *p = cmd_line;

   while (*p && argc < 16) {

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

   if (!cmd_argv[0][0]) {
      return;
   }

   if (!strcmp(cmd_argv[0], "cd")) {

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

   // printf("args(%i):\n", argc);
   // for (int i = 0; cmd_argv[i] != NULL; i++)
   //    printf("argv[%i] = '%s'\n", i, cmd_argv[i]);

   if (!strcmp(cmd_argv[0], "exit")) {
      printf("[shell] regular exit\n");
      exit(0);
   }

   int wstatus;
   int child_pid = fork();

   if (!child_pid) {
      execve(cmd_argv[0], NULL, NULL);
      int saved_errno = errno;
      perror(cmd_argv[0]);
      exit(saved_errno);
   }

   waitpid(child_pid, &wstatus, 0);
   printf("[shell] command exited with status: %d\n", WEXITSTATUS(wstatus));
}

int main(int argc, char **argv, char **env)
{
   char buf[256];
   char cwd[256];

   shell_env = env;

   printf("[PID: %i] Hello from ExOS's simple shell!\n", getpid());

   while (true) {

      if (getcwd(cwd, sizeof(cwd)) != cwd) {
         perror("Shell: getcwd failed");
         return 1;
      }

      printf("root@exOS:%s# ", cwd);
      fflush(stdout);

      int r = read(1, buf, sizeof(buf));
      buf[r] = 0;

      process_cmd_line(buf);
   }

   return 0;
}
