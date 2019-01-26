/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <assert.h>

#include <tilck/common/basic_defs.h> /* for STATIC_ASSERT and ARRAY_SIZE */

#define HIST_SIZE 16

#define SEQ_UP      "\033[A\0\0\0\0\0"
#define SEQ_DOWN    "\033[B\0\0\0\0\0"
#define SEQ_RIGHT   "\033[C\0\0\0\0\0"
#define SEQ_LEFT    "\033[D\0\0\0\0\0"
#define SEQ_DELETE  "\033[3~\0\0\0\0"
#define SEQ_HOME    "\033[H\0\0\0\0\0"
#define SEQ_END     "\033[F\0\0\0\0\0"

STATIC_ASSERT(sizeof(SEQ_UP) == 9);
STATIC_ASSERT(sizeof(SEQ_DOWN) == 9);
STATIC_ASSERT(sizeof(SEQ_RIGHT) == 9);
STATIC_ASSERT(sizeof(SEQ_LEFT) == 9);
STATIC_ASSERT(sizeof(SEQ_DELETE) == 9);
STATIC_ASSERT(sizeof(SEQ_HOME) == 9);
STATIC_ASSERT(sizeof(SEQ_END) == 9);

#define SN(s) (*(uint64_t*)(s))

#define WRITE_BS        "\033[D \033[D\0"
#define KEY_BACKSPACE   0x7f

static char cmd_history[HIST_SIZE][256];
static unsigned hist_count;
static unsigned curr_hist_cmd_to_show;
static int curr_line_pos;

static inline void put_in_history(const char *cmdline)
{
   strcpy(cmd_history[hist_count++ % HIST_SIZE], cmdline);
}

static const char *get_prev_cmd(unsigned count)
{
   if (!count || count > hist_count || count > HIST_SIZE)
      return NULL;

   return cmd_history[(hist_count - count) % HIST_SIZE];
}

static inline void raw_mode_erase_last(void)
{
   write(1, WRITE_BS, 7);
}

static void erase_line_on_screen(int curr_cmd_len)
{
   for (; curr_cmd_len > 0; curr_cmd_len--) {
      raw_mode_erase_last();
   }
}

static uint64_t read_esc_seq(void)
{
   char c;
   int len = 0;
   char buf[8] = { 0 };

   buf[len++] = '\033';

   if (read(0, &c, 1) <= 0)
      return 0;

   if (c != '[')
      return 0; /* unknown escape sequence */

   buf[len++] = c;

   while (true) {

      if (read(0, &c, 1) <= 0)
         return 0;

      buf[len++] = c;

      if (0x40 <= c && c <= 0x7E && c != '[')
         break;

      if (len == 8)
         return 0; /* no more space in our 64-bit int (seq too long) */
   }

   return *(uint64_t *)buf;
}

static void
handle_seq_home(uint64_t seq, char *buf, int bs, char *c_cmd, int *c_cmd_len)
{
   for (int i = curr_line_pos - 1; i >= 0; i--)
      write(1, SEQ_LEFT, 3);

   curr_line_pos = 0;
}

static void
handle_seq_end(uint64_t seq, char *buf, int bs, char *c_cmd, int *c_cmd_len)
{
   for (int i = curr_line_pos; i <= *c_cmd_len - 1; i++)
      write(1, SEQ_RIGHT, 3);

   curr_line_pos = *c_cmd_len;
}

static void
handle_seq_delete(uint64_t seq, char *buf, int bs, char *c_cmd, int *c_cmd_len)
{
   if (!*c_cmd_len || curr_line_pos == *c_cmd_len)
      return;

   (*c_cmd_len)--;

   for (int i = curr_line_pos; i < *c_cmd_len + 1; i++) {
      buf[i] = buf[i+1];
   }

   buf[*c_cmd_len] = ' ';
   write(1, buf + curr_line_pos, *c_cmd_len - curr_line_pos + 1);

   for (int i = *c_cmd_len; i >= curr_line_pos; i--)
      write(1, SEQ_LEFT, 3);
}

static void
handle_seq_left(uint64_t seq, char *buf, int bs, char *c_cmd, int *c_cmd_len)
{
   if (!curr_line_pos)
      return;

   write(1, SEQ_LEFT, 3);
   curr_line_pos--;
}

static void
handle_seq_right(uint64_t seq, char *buf, int bs, char *c_cmd, int *c_cmd_len)
{
   if (curr_line_pos >= *c_cmd_len)
      return;

   write(1, SEQ_RIGHT, 3);
   curr_line_pos++;
}

static void
handle_seq_updown(uint64_t seq, char *buf, int bs, char *c_cmd, int *c_cmd_len)
{
   const char *cmd;

   if (curr_line_pos != *c_cmd_len - 1) {
      for (; curr_line_pos < *c_cmd_len; curr_line_pos++)
         write(1, SEQ_RIGHT, 3);
   }

   if (seq == SN(SEQ_UP)) {

      cmd = get_prev_cmd(curr_hist_cmd_to_show + 1);

      if (!cmd)
         return;

      if (!curr_hist_cmd_to_show) {
         buf[*c_cmd_len] = 0;
         strncpy(c_cmd, buf, bs);
      }

      curr_hist_cmd_to_show++;

   } else {

      cmd = get_prev_cmd(curr_hist_cmd_to_show - 1);

      if (cmd) {
         curr_hist_cmd_to_show--;
      } else {
         cmd = c_cmd;
         if (curr_hist_cmd_to_show == 1)
            curr_hist_cmd_to_show--;
      }
   }

   erase_line_on_screen(*c_cmd_len);
   strncpy(buf, cmd, bs);
   *c_cmd_len = strlen(buf);
   write(1, buf, *c_cmd_len);
   curr_line_pos = *c_cmd_len;
}

static struct {

   uint64_t seq;
   void (*fptr)(uint64_t, char *, int, char *, int *);

} handle_esc_seq_table[] = {

   {0, handle_seq_home},
   {0, handle_seq_end},
   {0, handle_seq_delete},
   {0, handle_seq_left},
   {0, handle_seq_right},
   {0, handle_seq_updown},
   {0, handle_seq_updown}
};

static void initialize_once_handle_esc_seq_table(void)
{
   static bool initalized;

   if (initalized)
      return;

   handle_esc_seq_table[0].seq = SN(SEQ_HOME);
   handle_esc_seq_table[1].seq = SN(SEQ_END);
   handle_esc_seq_table[2].seq = SN(SEQ_DELETE);
   handle_esc_seq_table[3].seq = SN(SEQ_LEFT);
   handle_esc_seq_table[4].seq = SN(SEQ_RIGHT);
   handle_esc_seq_table[5].seq = SN(SEQ_UP);
   handle_esc_seq_table[6].seq = SN(SEQ_DOWN);

   initalized = true;
}

static void
handle_esc_seq(char *buf, int buf_size, char *c_cmd, int *c_cmd_len)
{
   uint64_t seq = read_esc_seq();

   if (!seq)
      return;

   initialize_once_handle_esc_seq_table();

   for (size_t i = 0; i < ARRAY_SIZE(handle_esc_seq_table); i++)
      if (handle_esc_seq_table[i].seq == seq)
         handle_esc_seq_table[i].fptr(seq, buf, buf_size, c_cmd, c_cmd_len);
}

static void
handle_backspace(char *buf, int buf_size, char *c_cmd, int *c_cmd_len)
{
   if (!(*c_cmd_len) || !curr_line_pos)
      return;

   (*c_cmd_len)--;
   curr_line_pos--;
   raw_mode_erase_last();

   if (curr_line_pos == (*c_cmd_len))
      return;

   /* We have to shift left all the chars after curr_line_pos */
   for (int i = curr_line_pos; i < (*c_cmd_len) + 1; i++) {
      buf[i] = buf[i+1];
   }

   buf[(*c_cmd_len)] = ' ';
   write(1, buf + curr_line_pos, (*c_cmd_len) - curr_line_pos + 1);

   for (int i = (*c_cmd_len); i >= curr_line_pos; i--)
      write(1, SEQ_LEFT, 3);
}

static bool
handle_regular_char(char c, char *buf, int bs, char *c_cmd, int *c_cmd_len)
{
   int rc = write(1, &c, 1);

   if (rc < 0) {
      perror("write error");
      *c_cmd_len = rc;
      return false;
   }

   if (c == '\n')
      return false;

   if (curr_line_pos == (*c_cmd_len)) {

      buf[curr_line_pos++] = c;

   } else {

      /* We have to shift right all the chars after curr_line_pos */
      for (int i = (*c_cmd_len); i >= curr_line_pos; i--) {
         buf[i + 1] = buf[i];
      }

      buf[curr_line_pos] = c;

      write(1, buf + curr_line_pos + 1, (*c_cmd_len) - curr_line_pos);
      curr_line_pos++;

      for (int i = (*c_cmd_len); i >= curr_line_pos; i--)
         write(1, SEQ_LEFT, 3);
   }

   (*c_cmd_len)++;
   return true;
}

int read_command(char *buf, int buf_size)
{
   int c_cmd_len = 0;
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

   while (c_cmd_len < buf_size - 1) {

      rc = read(0, &c, 1);

      if (rc == 0)
         break;

      if (rc < 0) {
         perror("read error");
         c_cmd_len = rc;
         break;
      }

      if (c == KEY_BACKSPACE) {

         handle_backspace(buf, buf_size, curr_cmd, &c_cmd_len);

      } else if (c == '\033') {

         handle_esc_seq(buf, buf_size, curr_cmd, &c_cmd_len);

      } else if (isprint(c) || isspace(c)) {

         if (!handle_regular_char(c, buf, buf_size, curr_cmd, &c_cmd_len))
            break;

      } else {

         /* just ignore everything else */
      }
   }

   buf[c_cmd_len >= 0 ? c_cmd_len : 0] = 0;

   if (c_cmd_len > 0)
      put_in_history(buf);

   tcsetattr(0, TCSAFLUSH, &orig_termios);
   return c_cmd_len;
}
