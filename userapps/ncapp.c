/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <locale.h>
#include <ncurses/ncurses.h>

#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define printc(name) printw(#name ": "); addch(name); printw("\n");

static const char *msg_next =
   "Press ANY key to move to the next screen or 'q' to exit";

static const char *msg_last = "Press ANY key to exit";


static void scr_special_chars(void)
{
   printw("Special characters:\n");

   /* Expected to work on all linux terminals. Work on Tilck. */
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
}

static void scr_colors(void)
{
   printw("Colors:\n");

   for (int i = 0; i < 8; i++)
      for (int j = 0; j < 8; j++)
         init_pair(1 + 8 * i + j, i, j);

   for (int i = 0; i < 8; i++) {

      for (int j = 0; j < 8; j++) {
         attron(COLOR_PAIR(1 + 8 * i + j));
         printw("a");
         attroff(COLOR_PAIR(1 + 8 * i + j));
      }

      printw("\n");
   }

   for (int i = 0; i < 8; i++)
      init_pair(i+1, i, COLOR_BLACK);

   for (int i = 0; i < 8; i++) {
      attron(COLOR_PAIR(i+1));
      printw("Normal ");
      attroff(COLOR_PAIR(i+1));
      attron(COLOR_PAIR(i+1) | A_BOLD);
      printw("Bright ");
      attroff(COLOR_PAIR(i+1) | A_BOLD);
      attron(COLOR_PAIR(i+1) | A_REVERSE);
      printw("Reverse");
      attroff(COLOR_PAIR(i+1) | A_REVERSE);
      printw(" ");
      attron(COLOR_PAIR(i+1) | A_REVERSE | A_BOLD);
      printw("Rev+bright\n");
      attroff(COLOR_PAIR(i+1) | A_REVERSE | A_BOLD);
   }
}

static void (*const funcs[])(void) = {
   &scr_special_chars,
   &scr_colors
};

int main()
{
   initscr();

   if (!has_colors()) {
      endwin();
      printf("No color support\n");
      return 1;
   }

   cbreak();
   noecho();
   start_color();

   for (unsigned i = 0; i < ARRAY_SIZE(funcs); i++) {

      clear();
      move(0, 0);

      funcs[i]();

      refresh();
      mvprintw(LINES - 1, 0, i < ARRAY_SIZE(funcs) - 1 ? msg_next : msg_last);

      if (getch() == 'q')
         break;
   }

   endwin();
   printf("the application exited gracefully\n");
   return 0;
}
