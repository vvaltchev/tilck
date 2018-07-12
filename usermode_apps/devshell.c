
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>

#define MAX_ARGS 16

char cmd_arg_buffers[MAX_ARGS][256];
char *cmd_argv[MAX_ARGS];
char **shell_env;

void run_if_known_command(const char *cmd);
int read_command(char *buf, int buf_size);

void shell_builtin_cd(int argc)
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

bool file_exists(const char *filepath)
{
   struct stat statbuf;
   int rc = stat(filepath, &statbuf);
   return !rc;
}

void process_cmd_line(const char *cmd_line)
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

   int wstatus;
   int child_pid = fork();

   if (!child_pid) {

      run_if_known_command(cmd_argv[0]);

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

   waitpid(child_pid, &wstatus, 0);

   if (WEXITSTATUS(wstatus))
      printf("[shell] command exited with status: %d\n", WEXITSTATUS(wstatus));
}

int main(int argc, char **argv, char **env)
{
   char buf[256];
   char cwd[256];

   shell_env = env;

   printf("[PID: %i] Hello from ExOS's simple dev-shell!\n", getpid());

   if (argc > 2 && !strcmp(argv[1], "-c")) {
      printf("[shell] Executing built-in command '%s'\n", argv[2]);
      run_if_known_command(argv[2]);
      printf("[shell] Unknown built-in command '%s'\n", argv[2]);
      exit(1);
   }

   while (true) {

      if (getcwd(cwd, sizeof(cwd)) != cwd) {
         perror("Shell: getcwd failed");
         return 1;
      }

      printf("root@exOS:%s# ", cwd);
      fflush(stdout);

      int rc = read_command(buf, sizeof(buf));

      if (rc < 0) {
         printf("I/O error\n");
         break;
      }

      if (!rc)
         continue;

      process_cmd_line(buf);
   }

   return 0;
}
