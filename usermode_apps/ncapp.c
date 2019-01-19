/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <locale.h>
#include <ncurses/ncurses.h>

#define printc(name) printw(#name ": "); addch(name); printw("\n");

int main()
{
   initscr();
   cbreak();

   printw("Special characters:\n");

   printc(ACS_HLINE);
   printc(ACS_LLCORNER);
   printc(ACS_ULCORNER);
   printc(ACS_VLINE);
   printc(ACS_LRCORNER);
   printc(ACS_URCORNER);
   printc(ACS_LTEE);
   printc(ACS_RTEE);
   printc(ACS_BTEE);
   printc(ACS_TTEE);
   printc(ACS_PLUS);

   printc(ACS_DIAMOND);

   refresh();
   getch();
   endwin();
   return 0;
}
