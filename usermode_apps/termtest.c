
#include <stdio.h>

#include <termios.h>
#include <unistd.h>

struct termios orig_termios;

void term_set_raw_mode(void)
{
   struct termios t;
   tcgetattr(0, &t);
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

int main()
{
   int ret;
   char buf[32];

   save_termios();
   term_set_raw_mode();

   ret = read(0, buf, 32);

   printf("read(%d): ", ret);

   for (int i = 0; i < ret; i++)
      printf("0x%x (%c), ", buf[i], buf[i]);

   printf("\n");
   restore_termios();

   return 0;
}
