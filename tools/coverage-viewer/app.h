/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include "model.h"
#include "scrollview.h"
#include "source_file.h"

#include <curses.h>

#include <map>
#include <vector>

/*
 * The interactive application: owns the curses screen, the navigation
 * stack, and the four views (directory overview, file list, source,
 * function list) that mirror the genhtml report. Rendering goes through
 * the incremental scroll_view; only the header/footer chrome is redrawn
 * on a view change.
 */
class app {

public:

   explicit app(const coverage_model &model) : m(model) {}

   void run();

private:

   enum class view_kind { dir_list, file_list, source, func_list };

   struct frame {
      view_kind kind;
      int dir_idx = -1;        /* for file_list */
      int file_idx = -1;       /* for source / func_list */
      int sort_mode = 0;       /* list views: see sort_modes()/sort_label() */
      int top = 0, sel = 0, hoff = 0;
   };

   const coverage_model &m;
   std::vector<frame> stack;
   std::vector<int> rows;              /* current view backing indices */
   std::map<int, source_file> srcs;    /* file_idx -> loaded source */

   WINDOW *body = nullptr;
   scroll_view sv;

   frame &cur() { return stack.back(); }

   void layout();
   void enter_view();
   void push(view_kind k, int dir_idx, int file_idx);
   void back();
   void toggle_source_funcs();
   void on_enter();
   void handle_key(int ch);

   void build_rows();
   int sort_modes() const;          /* number of sort orders for cur view */
   const char *sort_label() const;  /* name of the active sort order */
   void cycle_sort();
   void show_help();
   int source_lines(int file_idx);
   const source_file &source_for(int file_idx);

   void draw_header();
   void draw_footer();
   void draw_colhdr();

   void draw_row(WINDOW *w, int y, int idx, int hoff, bool sel);
   void draw_dir_row(WINDOW *w, int y, int idx, int hoff, bool sel);
   void draw_file_row(WINDOW *w, int y, int idx, int hoff, bool sel);
   void draw_source_row(WINDOW *w, int y, int idx, int hoff, bool sel);
   void draw_func_row(WINDOW *w, int y, int idx, int hoff, bool sel);
};
