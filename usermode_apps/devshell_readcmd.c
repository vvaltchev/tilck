
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>

#define KEY_UP    "\033[A"
#define KEY_DOWN  "\033[B"
#define KEY_RIGHT "\033[C"
#define KEY_LEFT  "\033[D"
#define KEY_ERASE 0x7f

#define HIST_SIZE 8

char cmd_history[HIST_SIZE][256];
unsigned hist_count = 0;

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
   char curr_cmd[buf_size]; // VLA

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

   if (ret > 0)
      put_in_history(buf);

   tcsetattr(0, TCSAFLUSH, &orig_termios);
   return ret;
}
