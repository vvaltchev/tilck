
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <termios.h>
#include <unistd.h>

#include <exos/common/debug/termios_debug.c.h>

struct termios orig_termios;

void term_set_raw_mode(void)
{
   struct termios t = orig_termios;

   // "Minimal" raw mode
   //t.c_lflag &= ~(ECHO | ICANON);

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

   ret = read(0, buf, 32);

   printf("read(%d): ", ret);

   for (int i = 0; i < ret; i++)
      if (isprint(buf[i]))
         printf("0x%x (%c), ", buf[i], buf[i]);
      else
         printf("0x%x, ", buf[i]);

   printf("\n");
}

void echo_read(void)
{
   int ret;
   char buf[16];

   while (1) {

      ret = read(0, buf, sizeof(buf));
      write(1, buf, ret);

      if (ret == 1 && (buf[0] == '\n' || buf[0] == '\r'))
         break;
   }
}

int main(int argc, char ** argv)
{
   save_termios();

   if (argc > 1 && !strcmp(argv[1], "--show")) {
      debug_dump_termios(&orig_termios);
      return 0;
   }

   printf("Setting tty to 'raw' mode\n");
   term_set_raw_mode();

   if (argc > 1 && !strcmp(argv[1], "--echo")) {
      echo_read();
   } else {
      one_read();
   }

   restore_termios();
   printf("\rOriginal tty mode restored\n");
   return 0;
}
