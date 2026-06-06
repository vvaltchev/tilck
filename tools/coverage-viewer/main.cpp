/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * coverage-viewer: a terminal UI to browse LCOV coverage data, meant to
 * be a functional equivalent of the genhtml HTML report (see
 * docs/coverage-viewer-feature-spec.md). It reads an LCOV tracefile
 * (coverage.info) and the source files it references.
 *
 * This file currently holds only the skeleton: argument handling plus
 * curses init/teardown. The model parser and the views are added in the
 * following commits.
 */

#include <curses.h>

#include <cstdio>
#include <string>

static const char *
pick_info_path(int argc, char **argv)
{
   if (argc > 1)
      return argv[1];

   return "coverage.info";
}

static void
curses_init()
{
   initscr();
   cbreak();
   noecho();
   keypad(stdscr, TRUE);
   curs_set(0);
}

int
main(int argc, char **argv)
{
   const std::string info_path = pick_info_path(argc, argv);

   curses_init();

   mvprintw(0, 0, "coverage-viewer (skeleton)");
   mvprintw(2, 0, "coverage file: %s", info_path.c_str());
   mvprintw(4, 0, "Press 'q' to quit.");
   refresh();

   while (true) {

      const int ch = getch();

      if (ch == 'q' || ch == 'Q')
         break;
   }

   endwin();
   printf("coverage-viewer: nothing to show yet (skeleton build)\n");
   return 0;
}
