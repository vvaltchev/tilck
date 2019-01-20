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

   /* Expected to work on all linux terminals */
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

   /* Mostly work on all linux terminals. Work on Tilck. */
   printc(ACS_DIAMOND);
   printc(ACS_CKBOARD);
   printc(ACS_DEGREE);
   printc(ACS_PLMINUS);
   printc(ACS_BULLET);

   /* Not expected to work on all linux terminals. Work on Tilck. */
   printc(ACS_LARROW);
   printc(ACS_RARROW);
   printc(ACS_DARROW);
   printc(ACS_UARROW);
   printc(ACS_BOARD);
   printc(ACS_BLOCK);

   refresh();
   getch();
   endwin();
   return 0;
}
