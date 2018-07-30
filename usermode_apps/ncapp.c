
#include <stdio.h>
#include <ncurses/ncurses.h>

int main()
{
   initscr();

   cbreak();
   printw("Hello World !!!");
   getch();

   endwin();
   return 0;
}
