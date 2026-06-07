/* SPDX-License-Identifier: BSD-2-Clause */

#include "scrollview.h"

#include <algorithm>
#include <utility>

void
scroll_view::init(WINDOW *win, draw_row_fn draw_cb)
{
   w = win;
   draw = std::move(draw_cb);

   /*
    * idlok lets ncurses use the terminal's insert/delete-line + scroll
    * region to satisfy our explicit wscrl() calls. scrollok stays OFF
    * here (so painting a row's rightmost cell can't auto-scroll the
    * list); apply() turns it on only momentarily around its wscrl().
    */
   idlok(w, TRUE);
   keypad(w, TRUE);
}

void
scroll_view::set_on_change(std::function<void()> cb)
{
   on_change = std::move(cb);
}

void
scroll_view::notify()
{
   if (on_change)
      on_change();
}

int
scroll_view::body_h() const
{
   return getmaxy(w);
}

int
scroll_view::max_top() const
{
   const int m = n - body_h();
   return m > 0 ? m : 0;
}

void
scroll_view::clear_line(int y)
{
   wmove(w, y, 0);
   wclrtoeol(w);
}

void
scroll_view::draw_at(int idx, bool selected)
{
   const int y = idx - tp;

   if (y < 0 || y >= body_h())
      return;

   if (idx < n)
      draw(w, y, idx, hoff, selected);
   else
      clear_line(y);
}

void
scroll_view::redraw()
{
   const int h = body_h();

   for (int y = 0; y < h; y++) {

      const int idx = tp + y;

      if (idx < n)
         draw(w, y, idx, hoff, idx == sel);
      else
         clear_line(y);
   }

   wnoutrefresh(w);
   notify();
}

/*
 * Move to (new_top, new_sel) with the minimal screen update:
 *  - same viewport       -> repaint just the old and new selected rows;
 *  - small viewport shift -> hardware-scroll + repaint exposed rows;
 *  - large shift          -> one body repaint.
 */
void
scroll_view::apply(int new_top, int new_sel)
{
   const int h = body_h();

   new_top = std::max(0, std::min(new_top, max_top()));
   new_sel = std::max(0, std::min(new_sel, n > 0 ? n - 1 : 0));

   const int old_sel = sel;
   const int dt = new_top - tp;

   if (dt == 0) {

      tp = new_top;
      sel = new_sel;

      if (old_sel != new_sel) {
         draw_at(old_sel, false);
         draw_at(new_sel, true);
      }

      wnoutrefresh(w);
      notify();
      return;
   }

   if (std::abs(dt) >= h) {
      tp = new_top;
      sel = new_sel;
      redraw();   /* redraw() calls notify() */
      return;
   }

   /* Native scroll by dt, then repaint only the newly exposed rows.
    *
    * scrollok must be on for wscrl() to do anything, but it must be off
    * the rest of the time, otherwise drawing a row's rightmost cell
    * auto-scrolls the whole list. So enable it only around wscrl(). */
   tp = new_top;
   sel = new_sel;
   scrollok(w, TRUE);
   wscrl(w, dt);
   scrollok(w, FALSE);

   if (dt > 0) {
      for (int y = h - dt; y < h; y++)
         draw_at(tp + y, tp + y == sel);
   } else {
      for (int y = 0; y < -dt; y++)
         draw_at(tp + y, tp + y == sel);
   }

   /* Un-highlight the old selection (if still visible) and ensure the new
    * one is highlighted; both are no-ops if already handled above. */
   if (old_sel != sel)
      draw_at(old_sel, false);

   draw_at(sel, true);
   wnoutrefresh(w);
   notify();
}

void
scroll_view::set_rows(int count)
{
   n = count;

   if (sel >= n)
      sel = n > 0 ? n - 1 : 0;

   if (tp > max_top())
      tp = max_top();

   redraw();
}

void
scroll_view::move_sel(int delta)
{
   if (n == 0)
      return;

   const int new_sel = std::max(0, std::min(sel + delta, n - 1));

   if (new_sel == sel)
      return;

   int new_top = tp;
   const int h = body_h();

   if (new_sel < tp)
      new_top = new_sel;
   else if (new_sel >= tp + h)
      new_top = new_sel - h + 1;

   apply(new_top, new_sel);
}

void
scroll_view::scroll_rows(int delta)
{
   if (n == 0)
      return;

   int new_top = std::max(0, std::min(tp + delta, max_top()));
   int new_sel = sel;
   const int h = body_h();

   /* Keep the selection within the new viewport. */
   if (new_sel < new_top)
      new_sel = new_top;
   else if (new_sel >= new_top + h)
      new_sel = new_top + h - 1;

   apply(new_top, new_sel);
}

void
scroll_view::page(int dir)
{
   const int h = body_h();
   apply(tp + dir * h, sel + dir * h);
}

void
scroll_view::to_first()
{
   apply(0, 0);
}

void
scroll_view::to_last()
{
   apply(max_top(), n > 0 ? n - 1 : 0);
}

void
scroll_view::goto_index(int idx)
{
   idx = std::max(0, std::min(idx, n > 0 ? n - 1 : 0));

   int new_top = tp;
   const int h = body_h();

   if (idx < tp || idx >= tp + h)
      new_top = idx - h / 2;   /* center the target */

   apply(new_top, idx);
}

void
scroll_view::h_scroll(int delta)
{
   const int new_off = std::max(0, hoff + delta);

   if (new_off == hoff)
      return;

   hoff = new_off;
   redraw();
}

void
scroll_view::set_h_off(int off)
{
   hoff = std::max(0, off);
   redraw();
}

void
scroll_view::restore(int top_row, int sel_row, int h_off_)
{
   tp = std::max(0, std::min(top_row, max_top()));
   sel = std::max(0, std::min(sel_row, n > 0 ? n - 1 : 0));
   hoff = std::max(0, h_off_);
   redraw();
}
