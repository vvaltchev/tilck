
#include <stdio.h>
#include <ncurses/ncurses.h>

int main()
{
   filter();
   initscr();

   cbreak();
   printw("Hello World !!!");
   getch();

   endwin();
   return 0;
}
