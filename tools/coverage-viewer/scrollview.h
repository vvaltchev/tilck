/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <curses.h>

#include <functional>

/*
 * An incrementally-updated scrollable list over a real ncurses WINDOW.
 *
 * The design goal (see docs/plans/coverage-viewer-plan.md) is to NEVER
 * rebuild the whole screen on a state change. Moving the selection
 * repaints only the two affected rows; scrolling past an edge uses the
 * terminal's own scroll (wscrl + idlok) and repaints only the newly
 * exposed line(s). A full body repaint happens only on enter, a page
 * jump, a horizontal scroll, or a resize.
 *
 * The widget is data-agnostic: it knows the row count and asks a
 * callback to paint one row at a time.
 */
class scroll_view {

public:

   /*
    * Paint exactly one row: model item `idx` at window line `y`, at the
    * current horizontal offset, drawn `selected` or not. The callback
    * owns the whole physical line (it should clear to end-of-line).
    */
   using draw_row_fn =
      std::function<void(WINDOW *w, int y, int idx, int h_off, bool sel)>;

   void init(WINDOW *win, draw_row_fn draw_cb);

   /* Model size changed: clamp and fully repaint the body. */
   void set_rows(int count);

   /* Full body repaint (enter / resize / sort change). */
   void redraw();

   /* Incremental selection move by `delta` rows. */
   void move_sel(int delta);

   /* Scroll the viewport by `delta` rows (selection follows into view). */
   void scroll_rows(int delta);

   void page(int dir);          /* dir = +1 / -1 */
   void to_first();
   void to_last();
   void goto_index(int idx);

   void h_scroll(int delta);    /* horizontal; repaints the body */
   void set_h_off(int off);

   int selected() const { return sel; }
   int top() const { return tp; }
   int h_off() const { return hoff; }
   int rows() const { return n; }

   /* Restore a saved scroll position (navigation stack). */
   void restore(int top_row, int sel_row, int h_off_);

private:

   int body_h() const;
   int max_top() const;
   void apply(int new_top, int new_sel);   /* the minimal-update core */
   void draw_at(int idx, bool sel);        /* draw row idx at its y */
   void clear_line(int y);

   WINDOW *w = nullptr;
   draw_row_fn draw;
   int n = 0;       /* number of model rows */
   int tp = 0;      /* first visible model index */
   int sel = 0;     /* selected model index */
   int hoff = 0;    /* horizontal offset */
};
