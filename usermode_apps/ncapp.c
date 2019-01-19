/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <locale.h>
#include <ncurses/ncurses.h>

static void draw_rect(int y1, int x1, int y2, int x2)
{
   mvhline(y1, x1, 0, x2-x1);
   mvhline(y2, x1, 0, x2-x1);
   mvvline(y1, x1, 0, y2-y1);
   mvvline(y1, x2, 0, y2-y1);

   mvaddch(y1, x1, ACS_ULCORNER);
   mvaddch(y2, x1, ACS_LLCORNER);
   mvaddch(y1, x2, ACS_URCORNER);
   mvaddch(y2, x2, ACS_LRCORNER);
}

int main()
{
   initscr();
   cbreak();

   printw("Hello World !!!");

   draw_rect(3, 3, 10, 30);

   refresh();
   getch();
   endwin();
   return 0;
}
