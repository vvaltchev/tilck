
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <assert.h>

#define HIST_SIZE 16

#define SEQ_UP      "\033[A\0\0\0\0\0"
#define SEQ_DOWN    "\033[B\0\0\0\0\0"
#define SEQ_RIGHT   "\033[C\0\0\0\0\0"
#define SEQ_LEFT    "\033[D\0\0\0\0\0"
#define SEQ_DELETE  "\033[\x7f\0\0\0\0\0"
#define SEQ_HOME    "\033[H\0\0\0\0\0"
#define SEQ_END     "\033[F\0\0\0\0\0"

#define SN(s) (*(uint64_t*)(s))

#define WRITE_BS      "\033[D \033[D\0"
#define KEY_BACKSPACE 0x7f

char cmd_history[HIST_SIZE][256];
unsigned hist_count;
unsigned curr_hist_cmd_to_show;
int curr_line_pos;

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
   write(1, WRITE_BS, 7);
}

void erase_line_on_screen(int curr_cmd_len)
{
   for (; curr_cmd_len > 0; curr_cmd_len--) {
      raw_mode_erase_last();
   }
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

   //printf("\n[0x%x]\n", ret);
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

   if (seq == SN(SEQ_HOME)) {

      for (int i = curr_line_pos - 1; i >= 0; i--)
         write(1, SEQ_LEFT, 3);

      curr_line_pos = 0;
      return;
   }

   if (seq == SN(SEQ_END)) {

      for (int i = curr_line_pos; i <= *curr_cmd_len - 1; i++)
         write(1, SEQ_RIGHT, 3);

      curr_line_pos = *curr_cmd_len;
      return;
   }

   if (seq == SN(SEQ_DELETE)) {

      if (!*curr_cmd_len || curr_line_pos == *curr_cmd_len)
         return;

      (*curr_cmd_len)--;

      for (int i = curr_line_pos; i < *curr_cmd_len + 1; i++) {
         buf[i] = buf[i+1];
      }

      buf[*curr_cmd_len] = ' ';
      write(1, buf + curr_line_pos, *curr_cmd_len - curr_line_pos + 1);

      for (int i = *curr_cmd_len; i >= curr_line_pos; i--)
         write(1, SEQ_LEFT, 3);

      return;
   }

   if (seq == SN(SEQ_LEFT) || seq == SN(SEQ_RIGHT)) {

      if (seq == SN(SEQ_LEFT)) {

         if (!curr_line_pos)
            return;

         write(1, SEQ_LEFT, 3);
         curr_line_pos--;

      } else {

         if (curr_line_pos >= *curr_cmd_len)
            return;

         write(1, SEQ_RIGHT, 3);
         curr_line_pos++;
      }

      return;
   }

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

      erase_line_on_screen(*curr_cmd_len);
      strncpy(buf, cmd, buf_size);
      *curr_cmd_len = strlen(buf);
      write(1, buf, *curr_cmd_len);
      curr_line_pos = *curr_cmd_len;
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
   curr_line_pos = 0;

   while (ret < buf_size - 1) {

      rc = read(0, &c, 1);

      if (rc <= 0) {
         if (rc < 0) {
            perror("read error");
            ret = rc;
         }
         goto out;
      }

      if (c == '\t')
         continue; /* ignore TABs */

      //printf("\n[0x%x]\n", c);

      if (c == KEY_BACKSPACE) {

         if (!ret || !curr_line_pos)
            continue;

         ret--;
         curr_line_pos--;
         raw_mode_erase_last();

         if (curr_line_pos == ret)
            continue;

         /* We have to shift left all the chars after curr_line_pos */
         for (int i = curr_line_pos; i < ret + 1; i++) {
            buf[i] = buf[i+1];
         }

         buf[ret] = ' ';
         write(1, buf + curr_line_pos, ret - curr_line_pos + 1);

         for (int i = ret; i >= curr_line_pos; i--)
            write(1, SEQ_LEFT, 3);

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

         if (curr_line_pos == ret) {

            buf[curr_line_pos++] = c;

         } else {

            /* We have to shift right all the chars after curr_line_pos */
            for (int i = ret; i >= curr_line_pos; i--) {
               buf[i + 1] = buf[i];
            }

            buf[curr_line_pos] = c;

            write(1, buf + curr_line_pos + 1, ret - curr_line_pos);
            curr_line_pos++;

            for (int i = ret; i >= curr_line_pos; i--)
               write(1, SEQ_LEFT, 3);
         }

         ret++;
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
