/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * coverage-viewer: a terminal UI to browse LCOV coverage data, meant to
 * be a functional equivalent of the genhtml HTML report (see
 * docs/coverage-viewer-feature-spec.md). It reads an LCOV tracefile
 * (coverage.info) and the source files it references.
 *
 * The model parser is in place; the curses views are added next. For
 * now the TUI shows a summary placeholder, and `--dump` prints the
 * parsed totals (used to verify parity with the HTML report).
 */

#include "model.h"

#include <curses.h>

#include <cstdio>
#include <string>

struct cli_args {
   std::string info_path = "coverage.info";
   bool dump = false;
};

static cli_args
parse_args(int argc, char **argv)
{
   cli_args a;

   for (int i = 1; i < argc; i++) {

      const std::string arg = argv[i];

      if (arg == "--dump")
         a.dump = true;
      else
         a.info_path = arg;
   }

   return a;
}

static void
dump_model(const coverage_model &m)
{
   printf("coverage.info: %s\n", m.info_path.c_str());
   printf("test: %s   date: %s\n", m.test_name.c_str(), m.date.c_str());
   printf("files: %zu   dirs: %zu\n", m.files.size(), m.dirs.size());
   printf("lines:     %d / %d  (%.1f %%)\n",
          m.lh, m.lf, cov_rate(m.lh, m.lf));
   printf("functions: %d / %d  (%.1f %%)\n",
          m.fnh, m.fnf, cov_rate(m.fnh, m.fnf));
   printf("\n%-44s %8s %8s\n", "directory", "lines", "funcs");

   for (const dir_cov &d : m.dirs)
      printf("%-44s %4d/%-4d %4d/%-4d\n",
             d.path.c_str(), d.lh, d.lf, d.fnh, d.fnf);
}

static void
run_tui(const coverage_model &m)
{
   initscr();
   cbreak();
   noecho();
   keypad(stdscr, TRUE);
   curs_set(0);

   mvprintw(0, 0, "coverage-viewer  -  %s", m.info_path.c_str());
   mvprintw(2, 0, "Loaded %zu files in %zu directories.",
            m.files.size(), m.dirs.size());
   mvprintw(3, 0, "Lines: %d / %d (%.1f %%)   Functions: %d / %d (%.1f %%)",
            m.lh, m.lf, cov_rate(m.lh, m.lf),
            m.fnh, m.fnf, cov_rate(m.fnh, m.fnf));
   mvprintw(5, 0, "Views coming next. Press 'q' to quit.");
   refresh();

   while (true) {

      const int ch = getch();

      if (ch == 'q' || ch == 'Q')
         break;
   }

   endwin();
}

int
main(int argc, char **argv)
{
   const cli_args args = parse_args(argc, argv);

   coverage_model m;
   std::string err;

   if (!load_coverage(args.info_path, m, err)) {
      fprintf(stderr, "coverage-viewer: %s\n", err.c_str());
      return 1;
   }

   if (args.dump) {
      dump_model(m);
      return 0;
   }

   run_tui(m);
   return 0;
}
