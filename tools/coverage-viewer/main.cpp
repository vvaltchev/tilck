/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * coverage-viewer: a terminal UI to browse LCOV coverage data, meant to
 * be a functional equivalent of the genhtml HTML report (see
 * docs/coverage-viewer-feature-spec.md). It reads an LCOV tracefile
 * (coverage.info) and the source files it references.
 *
 * At this stage main.cpp drives a scrollable directory list as a demo of
 * the incremental ScrollView + color modules. The proper four views and
 * navigation are layered on next.
 */

#include "colors.h"
#include "model.h"
#include "scrollview.h"

#include <curses.h>

#include <cstdio>
#include <cstring>
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

/* ---- demo TUI: a scrollable directory list ---------------------------- */

static const coverage_model *g_model = nullptr;

static void
draw_dir_row(WINDOW *w, int y, int idx, int h_off, bool sel)
{
   (void)h_off;

   const dir_cov &d = g_model->dirs[idx];
   const int width = getmaxx(w);
   const double p = cov_rate(d.lh, d.lf);

   char metrics[40];
   snprintf(metrics, sizeof(metrics), "%6d/%-6d %5.1f%%", d.lh, d.lf, p);

   const int mlen = static_cast<int>(strlen(metrics));
   const int name_room = width - mlen - 2;

   if (sel) {

      const chtype a = cv_attr(CVP_SEL);
      wattron(w, a);
      mvwhline(w, y, 0, ' ', width);

      if (name_room > 0)
         mvwaddnstr(w, y, 0, d.path.c_str(), name_room);

      mvwaddstr(w, y, width - mlen, metrics);
      wattroff(w, a);
      return;
   }

   wmove(w, y, 0);
   wclrtoeol(w);

   if (name_room > 0)
      mvwaddnstr(w, y, 0, d.path.c_str(), name_room);

   const chtype a = bucket_attr(cov_bucket(d.lh, d.lf));
   wattron(w, a);
   mvwaddstr(w, y, width - mlen, metrics);
   wattroff(w, a);
}

static void
draw_chrome(const coverage_model &m)
{
   const int width = getmaxx(stdscr);
   const chtype t = cv_attr(CVP_TITLE);

   wattron(stdscr, t);
   mvwhline(stdscr, 0, 0, ' ', width);
   mvwprintw(stdscr, 0, 1,
             "coverage-viewer  -  %d dirs  lines %.1f%%  funcs %.1f%%",
             static_cast<int>(m.dirs.size()),
             cov_rate(m.lh, m.lf), cov_rate(m.fnh, m.fnf));
   wattroff(stdscr, t);

   mvwprintw(stdscr, LINES - 1, 0,
             "j/k move  PgUp/PgDn  g/G  q quit");
   wclrtoeol(stdscr);
   wnoutrefresh(stdscr);
}

static void
run_tui(const coverage_model &m)
{
   g_model = &m;

   initscr();
   cbreak();
   noecho();
   keypad(stdscr, TRUE);
   curs_set(0);
   colors_init();

   WINDOW *body = newwin(LINES - 2, COLS, 1, 0);

   scroll_view sv;
   sv.init(body, draw_dir_row);

   draw_chrome(m);
   sv.set_rows(static_cast<int>(m.dirs.size()));
   doupdate();

   bool running = true;

   while (running) {

      const int ch = getch();

      switch (ch) {

         case 'q':
         case 'Q':
            running = false;
            break;

         case KEY_DOWN:
         case 'j':
            sv.move_sel(1);
            break;

         case KEY_UP:
         case 'k':
            sv.move_sel(-1);
            break;

         case KEY_NPAGE:
            sv.page(1);
            break;

         case KEY_PPAGE:
            sv.page(-1);
            break;

         case 'g':
         case KEY_HOME:
            sv.to_first();
            break;

         case 'G':
         case KEY_END:
            sv.to_last();
            break;

         case KEY_RESIZE:
            delwin(body);
            body = newwin(LINES - 2, COLS, 1, 0);
            sv.init(body, draw_dir_row);
            sv.set_rows(static_cast<int>(m.dirs.size()));
            draw_chrome(m);
            break;

         default:
            continue;
      }

      doupdate();
   }

   delwin(body);
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
