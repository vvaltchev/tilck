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
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/kd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>

#ifdef USERMODE_APP
   /* The application is compiled with Tilck's build system */
   #include <tilck/common/debug/termios_debug.c.h>
#else
   #define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#endif

#define RDTSC() __builtin_ia32_rdtsc()

#define CSI_ERASE_DISPLAY          "\033[2J"
#define CSI_MOVE_CURSOR_TOP_LEFT   "\033[1;1H"

struct termios orig_termios;

void term_set_raw_mode(void)
{
   struct termios t = orig_termios;

   printf("Setting tty to 'raw' mode\n");

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
   ioctl(0, KDSKBMODE, K_XLATE);
}

void one_read(void)
{
   int ret;
   char buf[32];

   printf("one byte RAW read\n");
   term_set_raw_mode();

   ret = read(0, buf, 32);

   printf("read(%d): ", ret);

   for (int i = 0; i < ret; i++) {
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
   }

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
   char buf[32] = {0};
   int r;

   printf("Regular read in canonical mode\n");
   r = read(0, buf, 32);
   printf("read(%d): %s", r, buf);
}

void read_ttys0_canon_mode(void)
{
   char buf[32] = {0};
   int r, fd;

   fd = open("/dev/ttyS0", O_RDONLY);

   if (fd < 0) {
      perror("Open /dev/ttyS0 failed");
      return;
   }

   printf("Regular read from /dev/ttyS0 in canonical mode\n");

   r = read(fd, buf, 32);

   printf("read(%d): %s", r, buf);
   close(fd);
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
   ssize_t r, tot, written;

   if (ioctl(1, TIOCGWINSZ, &w) != 0) {
      perror("ioctl() failed");
      return;
   }

   tot = w.ws_row * w.ws_col;
   buf = malloc(tot);

   if (!buf) {
      fprintf(stderr, "Out of memory\n");
      return;
   }

   for (int i = 0; i < tot; i++) {
      buf[i] = letters[i % (sizeof(letters) - 1)];
   }

   printf("%s", CSI_ERASE_DISPLAY CSI_MOVE_CURSOR_TOP_LEFT);

   uint64_t start = RDTSC();

   for (int i = 0; i < iters; i++) {
      for (r = 0, written = 0; written < tot; written += r) {

         r = write(1, buf + written, tot - written);

         if (r < 0) {
            perror("write() failed");
            return;
         }
      }
   }

   uint64_t end = RDTSC();
   uint64_t c = (end - start) / iters;

   printf("Term size: %d rows x %d cols\n", w.ws_row, w.ws_col);
   printf("Screen redraw:       %10llu cycles\n", c);
   printf("Avg. character cost: %10llu cycles\n", c / (w.ws_row * w.ws_col));
   free(buf);
}

void read_nonblock(void)
{
   int rc;
   char buf[32];
   int saved_flags = fcntl(0, F_GETFL, 0);

   printf("Setting non-block mode for fd 0\r\n");
   rc = fcntl(0, F_SETFL, saved_flags | O_NONBLOCK);

   if (rc != 0) {
      fprintf(stderr, "fcntl() failed: %s\r\n", strerror(errno));
      return;
   }

   for (int i = 0; ; i++) {

      rc = read(0, buf, 1);

      if (rc >= 0) {

         buf[rc] = 0;
         printf("[iter %d] read() = %d [buf: '%s']\r\n", i, rc, buf);

         if (buf[0] == 'q')
            break;

      } else {
         printf("[iter %d] read() = %d (errno: %d => %s)\r\n",
                 i, rc, errno, strerror(errno));
         usleep(500*1000);
      }

   }

   // Restore the original flags
   rc = fcntl(0, F_SETFL, saved_flags);

   if (rc != 0)
      fprintf(stderr, "fcntl() failed: %s\r\n", strerror(errno));
}

void read_nonblock_rawmode(void)
{
   term_set_raw_mode();
   read_nonblock();
}

static void write_full_row(void)
{
   struct winsize w;
   ioctl(1, TIOCGWINSZ, &w);

   printf("Term size: %d rows x %d cols\n\n", w.ws_row, w.ws_col);

   printf("TEST 1) Full row with '-':\n");

   for (int i = 0; i < w.ws_col; i++)
      putchar('-');

   printf("[text after full row]\n\n\n");
   printf("TEST 2) Now full row with '-' + \\n\n");

   for (int i = 0; i < w.ws_col; i++)
      putchar('-');

   putchar('\n');
   printf("[text after full row]\n\n");
}

static void sleep_then_read(void)
{
   char buf[32] = {0};
   int rc;

   printf("sleep\n");
   sleep(2);
   printf("sleep done, reading\n");

   rc = read(0, buf, sizeof(buf));
   printf("read(): %d -> '%s'\n", rc, buf);
}

static void sym_read(void)
{
   char buf[32] = {0};
   int rc;

   if (!fork()) {
      printf("[child]\n");
      rc = read(0, buf, sizeof(buf));
      printf("[child] read(): %d -> '%s'\n", rc, buf);
      exit(0);
   }

   printf("[parent] reading...\n");
   rc = read(0, buf, sizeof(buf));
   printf("[parent] read(): %d -> '%s'\n", rc, buf);
   waitpid(-1, NULL, 0);
}

static void poll_and_read(void)
{
   char buf[32] = {0};
   int rc, pos = 0;
   struct pollfd fds[] = {
      { .fd = 0, .events = POLLIN, .revents = 0 }
   };

   printf("Setting TTY in raw mode\n");
   term_set_raw_mode();

   while (1) {

      rc = poll(fds, 1 /* nfds */, 1000 /* ms */);
      printf("poll() -> %d\r\n", rc);

      if (rc > 0) {

         if (fds[0].revents & POLLIN) {
            printf("fd %d -> POLLIN\r\n", fds[0].fd);
            break;
         }

         if (fds[0].revents & POLLPRI)
            printf("fd %d -> POLLPRI\r\n", fds[0].fd);

         if (fds[0].revents & POLLRDHUP)
            printf("fd %d -> POLLRDHUP\r\n", fds[0].fd);

         if (fds[0].revents & POLLERR)
            printf("fd %d -> POLLERR\r\n", fds[0].fd);

         if (fds[0].revents & POLLHUP)
            printf("fd %d -> POLLHUP\r\n", fds[0].fd);

         if (fds[0].revents & POLLNVAL)
            printf("fd %d -> POLLNVAL\r\n", fds[0].fd);
      }
   }

   printf("poll() said there's something to read. read():\r\n");

   for (pos = 0; pos < 32; pos++) {

      rc = read(0, buf + pos, 1);

      printf("read() -> %d\r\n", rc);
      printf("buf[%d]: 0x%x\r\n", pos, (unsigned)buf[pos]);

      rc = poll(fds, 1 /* nfds */, 50 /* ms */);
      printf("poll() -> %d\r\n", rc);

      if (rc > 0) {
         if (fds[0].revents & POLLIN) {
            printf("fd %d -> POLLIN\r\n", fds[0].fd);
            continue;
         }
      }

      printf("Nothing more to read, break\r\n");
      break;
   }
}

static void medium_raw_read(void)
{
   char c;
   int rc;

   if (ioctl(0, KDSKBMODE, K_MEDIUMRAW) != 0) {
      printf("Unable to set mediumraw mode.\r\n");
      return;
   }

   term_set_raw_mode();

   do {

      rc = read(0, &c, 1);

      if (!rc)
         break;

      if (c & 0x80)
         printf("released 0x%x", (unsigned char)(c & ~0x80));
      else
         printf("PRESSED 0x%x", (unsigned char)(c & ~0x80));

      printf("\r\n");

   } while (c != 0x10 /* q */);
}

#ifdef USERMODE_APP
static void dump_termios(void)
{
   debug_dump_termios(&orig_termios);
}
#endif

#define CMD_ENTRY(opt, func) { (opt), #func, &func }

static struct {

   const char *opt;
   const char *func_name;
   void (*func)(void);

} commands[] = {

   CMD_ENTRY("-r", one_read),
   CMD_ENTRY("-e", echo_read),
   CMD_ENTRY("-1", read_1_canon_mode),
   CMD_ENTRY("-c", read_canon_mode),
   CMD_ENTRY("-w", write_to_stdin),

#ifdef USERMODE_APP
   CMD_ENTRY("-s", dump_termios),
#endif

   CMD_ENTRY("-p", console_perf_test),
   CMD_ENTRY("-n", read_nonblock),
   CMD_ENTRY("-nr", read_nonblock_rawmode),
   CMD_ENTRY("-fr", write_full_row),
   CMD_ENTRY("-sr", sleep_then_read),
   CMD_ENTRY("-mr", sym_read),
   CMD_ENTRY("-cs", read_ttys0_canon_mode),
   CMD_ENTRY("-pr", poll_and_read),
   CMD_ENTRY("-xmr", medium_raw_read),
};

static void show_help(void)
{
   printf("Options:\n");

   for (size_t i = 0; i < ARRAY_SIZE(commands); i++) {
      printf("    %-3s  %s()\n", commands[i].opt, commands[i].func_name);
   }
}

int main(int argc, char ** argv)
{
   void (*cmdfunc)(void) = show_help;

   if (argc < 2) {
      show_help();
      return 1;
   }

   for (size_t i = 0; i < ARRAY_SIZE(commands); i++) {
      if (!strcmp(argv[1], commands[i].opt)) {
         cmdfunc = commands[i].func;
         break;
      }
   }

   save_termios();
   cmdfunc();
   restore_termios();

   if (cmdfunc != &show_help)
      printf("\rOriginal tty mode restored\n");

   return 0;
}
