
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
#include <termios.h>

#define MAX_ARGS 16
#define HIST_SIZE 8

char cmd_history[HIST_SIZE][256];
unsigned hist_count = 0;

char cmd_arg_buffers[MAX_ARGS][256];
char *cmd_argv[MAX_ARGS];
char **shell_env;

unsigned curr_hist_cmd_to_show;

void put_in_history(const char *cmdline)
{
   strcpy(cmd_history[hist_count++ % HIST_SIZE], cmdline);
}

const char *get_prev_cmd(unsigned count)
{
   if (!count || count > hist_count || count > HIST_SIZE)
      return NULL;

   return cmd_history[(hist_count - count) % HIST_SIZE];
}

void run_if_known_command(const char *cmd);

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

#define KEY_UP    "\033[A"
#define KEY_DOWN  "\033[B"
#define KEY_RIGHT "\033[C"
#define KEY_LEFT  "\033[D"
#define KEY_ERASE 0x7f

void raw_mode_erase_last(void)
{
   write(1, KEY_LEFT " " KEY_LEFT, 7);
}

int read_esc_seq(void)
{
   int rc;
   char seq[4];

   rc = read(0, &seq[0], 1);

   if (rc <= 0)
      return rc;

   if (seq[0] != '[')
      return 0;

   rc = read(0, &seq[1], 1);

   if (rc <= 0)
      return rc;

   if (seq[1] != 'A' && seq[1] != 'B' && seq[1] != 'C' && seq[1] != 'D')
      return 0;

   return seq[1];
}

int read_command(char *buf, int buf_size)
{
   int ret = 0;
   int rc;
   char c;
   struct termios orig_termios, t;
   char curr_cmd[256];

   tcgetattr(0, &orig_termios);

   t = orig_termios;
   t.c_iflag &= ~(BRKINT | INPCK | ISTRIP | IXON);
   t.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
   tcsetattr(0, TCSAFLUSH, &t);

   curr_hist_cmd_to_show = 0;

   while (ret < buf_size) {

      rc = read(0, &c, 1);

      if (rc <= 0) {
         if (rc < 0) {
            perror("read error");
            ret = rc;
         }
         goto out;
      }

      if (c == KEY_ERASE) {
         if (ret > 0) {
            ret--;
            raw_mode_erase_last();
         }
         continue;
      }

      if (c == '\033') {

         rc = read_esc_seq();

         if (rc < 0) {
            ret = -1;
            goto out;
         }

         if (!rc)
            continue;

         if (rc == 'A' || rc == 'B') {

            const char *cmd;

            if (rc == 'A') {

               cmd = get_prev_cmd(curr_hist_cmd_to_show + 1);

               if (!cmd)
                  continue;

               if (!curr_hist_cmd_to_show) {
                  buf[ret] = 0;
                  strncpy(curr_cmd, buf, sizeof(curr_cmd));
               }

               curr_hist_cmd_to_show++;

            } else {

               cmd = get_prev_cmd(curr_hist_cmd_to_show - 1);

               if (cmd) {
                  curr_hist_cmd_to_show--;
               } else {
                  cmd = curr_cmd;
                  if (curr_hist_cmd_to_show == 1)
                     curr_hist_cmd_to_show--;
               }
            }

            while (ret > 0) {
               raw_mode_erase_last();
               ret--;
            }

            strncpy(buf, cmd, buf_size);
            ret = strlen(cmd);
            write(1, buf, ret);
            continue;
         }
      }

      if (isprint(c) || isspace(c)) {

         rc = write(1, &c, 1);

         if (rc < 0) {
            perror("write error");
            ret = rc;
            goto out;
         }

         if (c == '\n')
            break;

         buf[ret++] = c;
         continue;
      }

   }

out:
   buf[ret >= 0 ? ret : 0] = 0;
   tcsetattr(0, TCSAFLUSH, &orig_termios);
   return ret;
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

      put_in_history(buf);
      process_cmd_line(buf);
   }

   return 0;
}
