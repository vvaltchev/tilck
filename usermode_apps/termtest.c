
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <unistd.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <exos/common/debug/termios_debug.c.h>

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
   int fd = open("./testdir/BBB", O_RDONLY);

   if (fd < 0) {
      printf("Unable to open ./testdir/BBB\n");
      return;
   }

   printf("read_1_canon_mode(): read 2 chars, one-by-one\n");

   r = read(0, buf, 1);
   show_read_res(r, buf[0]);

   // Read of other file, in order to check if the unread stuff will be kept
   r = read(fd, buf, 32);
   printf("other read(%d): %s\n", r, buf);

   r = read(0, buf, 1);
   show_read_res(r, buf[0]);

   close(fd);
}

int main(int argc, char ** argv)
{
   save_termios();

   if (argc > 1 && !strcmp(argv[1], "-s")) {
      debug_dump_termios(&orig_termios);
      return 0;
   }

   if (argc > 1 && !strcmp(argv[1], "-e")) {
      echo_read();
   } else if (argc > 1 && !strcmp(argv[1], "-1")) {
      read_1_canon_mode();
   } else {
      one_read();
   }

   restore_termios();
   printf("\rOriginal tty mode restored\n");
   return 0;
}
