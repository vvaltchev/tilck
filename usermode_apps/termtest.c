
#include <stdio.h>
#include <string.h>

#include <termios.h>
#include <unistd.h>

#include <exos/common/debug/termios_debug.c.h>

struct termios orig_termios;

void term_set_raw_mode(void)
{
   struct termios t = orig_termios;
   t.c_lflag &= ~(ECHO | ICANON);
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

int main(int argc, char ** argv)
{
   int ret;
   char buf[32];

   save_termios();

   if (argc > 1 && !strcmp(argv[1], "--show")) {
      debug_dump_termios(&orig_termios);
      return 0;
   }

   printf("Setting tty to 'raw' mode\n");
   term_set_raw_mode();

   ret = read(0, buf, 32);

   printf("read(%d): ", ret);

   for (int i = 0; i < ret; i++)
      printf("0x%x (%c), ", buf[i], buf[i]);

   printf("\n");

   printf("Restore the original tty mode\n");
   restore_termios();
   return 0;
}
