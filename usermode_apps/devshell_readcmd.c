
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>

#define HIST_SIZE 16

#define SEQ_UP    "\033[A\0\0\0\0\0"
#define SEQ_DOWN  "\033[B\0\0\0\0\0"
#define SEQ_RIGHT "\033[C\0\0\0\0\0"
#define SEQ_LEFT  "\033[D\0\0\0\0\0"
#define SEQ_BS    "\033[D \033[D\0"

#define SN(s) (*(uint64_t*)(s))

#define KEY_ERASE 0x7f

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
   write(1, SEQ_BS, 7);
}

uint64_t read_esc_seq(void)
{
   char c;
   int len;
   uint64_t ret = 0;

   ret |= '\033';

   if (read(0, &c, 1) <= 0)
      return 0;

   if (c != '[')
      return 0; /* unknown escape sequence */

   ret |= (c << 8);
   len = 2;

   while (1) {

      if (read(0, &c, 1) <= 0)
         return 0;

      ret |= (c << (8 * len));

     if (0x40 <= c && c <= 0x7E)
        break;

      if (len == 8)
         return 0; /* no more space in our 64-bit int (seq too long) */
   }

   return ret;
}

void handle_esc_seq(char *buf,
                    int buf_size,
                    char *curr_cmd,
                    int *curr_cmd_len)
{

   uint64_t seq = read_esc_seq();

   if (!seq)
      return;

   if (seq == SN(SEQ_UP) || seq == SN(SEQ_DOWN)) {

      const char *cmd;

      if (seq == SN(SEQ_UP)) {

         cmd = get_prev_cmd(curr_hist_cmd_to_show + 1);

         if (!cmd)
            return;

         if (!curr_hist_cmd_to_show) {
            buf[*curr_cmd_len] = 0;
            strncpy(curr_cmd, buf, buf_size);
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

      while (*curr_cmd_len > 0) {
         raw_mode_erase_last();
         (*curr_cmd_len)--;
      }

      strncpy(buf, cmd, buf_size);
      *curr_cmd_len = strlen(cmd);
      write(1, buf, *curr_cmd_len);
   }
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
         handle_esc_seq(buf, buf_size, curr_cmd, &ret);
         continue;
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
