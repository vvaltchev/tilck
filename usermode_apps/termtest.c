/* SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>

#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#ifdef USERMODE_APP
   /* It means that the application is compiled with Tilck's build system */
   #include <tilck/common/debug/termios_debug.c.h>
#endif

#define RDTSC() __builtin_ia32_rdtsc()

#define CSI_ERASE_DISPLAY          "\033[2J"
#define CSI_MOVE_CURSOR_TOP_LEFT   "\033[1;1H"

struct termios orig_termios;

void term_set_raw_mode(void)
{
   struct termios t = orig_termios;

   printf("Setting tty to 'raw' mode\n");

   // "Minimal" raw mode
   // t.c_lflag &= ~(ECHO | ICANON);

   // "Full" raw mode
   t.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
   t.c_oflag &= ~(OPOST);
   t.c_cflag |= (CS8);
   t.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

   tcsetattr(0, TCSAFLUSH, &t);
}

void save_termios(void)
{
   tcgetattr(0, &orig_termios);
}

void restore_termios(void)
{
   tcsetattr(0, TCSAFLUSH, &orig_termios);
}

void one_read(void)
{
   int ret;
   char buf[32];

   printf("one byte RAW read\n");
   term_set_raw_mode();

   ret = read(0, buf, 32);

   printf("read(%d): ", ret);

   for (int i = 0; i < ret; i++)
      if (buf[i] == '\033')
         printf("ESC ");
      else if (buf[i] == '\n')
         printf("NL ");
      else if (buf[i] == '\r')
         printf("CR ");
      else if (isprint(buf[i]))
         printf("%c ", buf[i]);
      else
         printf("[0x%x] ", buf[i]);

   printf("\n");
}

void echo_read(void)
{
   int ret;
   char buf[16];

   printf("echo_read()\n");
   term_set_raw_mode();

   while (1) {

      ret = read(0, buf, sizeof(buf));
      write(1, buf, ret);

      if (ret == 1 && (buf[0] == '\n' || buf[0] == '\r'))
         break;
   }
}

void show_read_res(int r, char c)
{
   if (r > 0)
      printf("read(%d): 0x%x\n", r, c);
   else
      printf("read(%d)\n", r);
}

void read_1_canon_mode(void)
{
   char buf[32] = {0};
   int r;

   printf("read_1_canon_mode(): read 2 chars, one-by-one\n");

   r = read(0, buf, 1);
   show_read_res(r, buf[0]);

   r = read(0, buf, 1);
   show_read_res(r, buf[0]);
}

void read_canon_mode(void)
{
   char buf[32];
   int r;

   printf("Regular read in canonical mode\n");
   r = read(0, buf, 32);
   buf[r] = 0;

   printf("read(%d): %s", r, buf);
}

void write_to_stdin(void)
{
   char c = 'a';
   int r;

   printf("Write 'a' to stdin\n");

   r = write(0, &c, 1);

   printf("write() -> %d\n", r);
   printf("now read 1 byte from stdin\n");

   r = read(0, &c, 1);

   printf("read(%d): 0x%x\n", r, c);
}

void console_perf_test(void)
{
   static const char letters[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

   const int iters = 10;
   struct winsize w;
   char *buf;

   ioctl(1, TIOCGWINSZ, &w);

   buf = malloc(w.ws_row * w.ws_col);

   if (!buf) {
      printf("Out of memory\n");
      return;
   }

   for (int i = 0; i < w.ws_row * w.ws_col; i++) {
      buf[i] = letters[i % (sizeof(letters) - 1)];
   }

   printf("%s", CSI_ERASE_DISPLAY CSI_MOVE_CURSOR_TOP_LEFT);

   uint64_t start = RDTSC();

   for (int i = 0; i < iters; i++) {
      write(1, buf, w.ws_row * w.ws_col);
   }

   uint64_t end = RDTSC();
   unsigned long long c = (end - start) / iters;

   printf("Term size: %d rows x %d cols\n", w.ws_row, w.ws_col);
   printf("Screen redraw:       %10llu cycles\n", c);
   printf("Avg. character cost: %10llu cycles\n", c / (w.ws_row * w.ws_col));
   free(buf);
}

void read_nonblock(void)
{
   int rc;
   char buf[256];
   int saved_flags = fcntl(0, F_GETFL, 0);

   printf("Setting non-block mode for fd 0\n");

   rc = fcntl(0, F_SETFL, saved_flags | O_NONBLOCK);

   if (rc != 0) {
      fprintf(stderr, "fcntl() failed: %s\n", strerror(errno));
      return;
   }

   for (int i = 0; ; i++) {
      rc = read(0, buf, sizeof(buf));

      if (rc >= 0) {
         buf[rc] = 0;
         printf("[iter %d] read() = %d [buf: '%s']\n", i, rc, buf);
      } else {
         printf("[iter %d] read() = %d (errno: %d => %s)\n",
                 i, rc, errno, strerror(errno));
      }

      usleep(500*1000);
   }

   // Restore the orignal flags
   rc = fcntl(0, F_SETFL, saved_flags);

   if (rc != 0)
      fprintf(stderr, "fcntl() failed: %s\n", strerror(errno));
}

void show_help_and_exit(void)
{
   printf("Options:\n");
   printf("    -r one_read()\n");
   printf("    -e echo_read()\n");
   printf("    -1 read_1_canon_mode()\n");
   printf("    -c read_canon_mode()\n");
   printf("    -w write_to_stdin()\n");

#ifdef USERMODE_APP
   printf("    -s debug_dump_termios()\n");
#endif

   printf("    -p console_perf_test()\n");
   printf("    -n read_nonblock()\n");
   exit(1);
}

int main(int argc, char ** argv)
{
   save_termios();

   if (argc < 2) {
      show_help_and_exit();
   }

   if (0) {
      /* Dummy, used just to allow all the below to be like "} else if (" */
   } else if (!strcmp(argv[1], "-e")) {
      echo_read();
   } else if (!strcmp(argv[1], "-1")) {
      read_1_canon_mode();
   } else if (!strcmp(argv[1], "-c")) {
      read_canon_mode();
   } else if (!strcmp(argv[1], "-r")) {
      one_read();
   } else if (!strcmp(argv[1], "-w")) {
      write_to_stdin();

#ifdef USERMODE_APP
   } else if (!strcmp(argv[1], "-s")) {
      debug_dump_termios(&orig_termios);
#endif

   } else if (!strcmp(argv[1], "-p")) {
      console_perf_test();
   } else if (!strcmp(argv[1], "-n")) {
      read_nonblock();
   } else {
      show_help_and_exit();
   }

   restore_termios();
   printf("\rOriginal tty mode restored\n");
   return 0;
}
