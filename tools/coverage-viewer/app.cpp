/* SPDX-License-Identifier: BSD-2-Clause */

#include "app.h"
#include "colors.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

/* Screen layout: title, totals, column header, body..., footer. */
static const int HDR_LINES = 3;
static const int FTR_LINES = 1;

/* ---- small drawing helpers ------------------------------------------- */

/* Place `s` in the field [x, x+fw), justified, clipped, with `attr`. */
static void
put_field(WINDOW *w, int y, int x, int fw, const std::string &s,
          chtype attr, bool right)
{
   if (fw <= 0)
      return;

   std::string t = s;

   if (static_cast<int>(t.size()) > fw)
      t = t.substr(0, fw);

   const int pad = fw - static_cast<int>(t.size());
   const std::string spaces(pad, ' ');
   const std::string out = right ? spaces + t : t + spaces;

   wattron(w, attr);
   mvwaddnstr(w, y, x, out.c_str(), fw);
   wattroff(w, attr);
}

static std::string
fmt_pct(double p, int total)
{
   char buf[16];

   if (total <= 0)
      return "-";

   snprintf(buf, sizeof(buf), "%.1f%%", p);
   return buf;
}

static std::string
fmt_count(int hit, int total)
{
   char buf[24];
   snprintf(buf, sizeof(buf), "%d/%d", hit, total);
   return buf;
}

/* A [####------] bar of `cells` cells, filled by rate, colored by bucket. */
static void
draw_bar(WINDOW *w, int y, int x, int cells, double p, bucket b, bool sel)
{
   const int filled = static_cast<int>(p / 100.0 * cells + 0.5);
   const chtype fill_attr = sel ? cv_attr(CVP_SEL) : bar_attr(b);
   const chtype frame_attr = sel ? cv_attr(CVP_SEL) : cv_attr(CVP_DIM);

   wattron(w, frame_attr);
   mvwaddch(w, y, x, '[');
   wattroff(w, frame_attr);

   for (int i = 0; i < cells; i++) {

      const bool on = i < filled;
      const chtype a = on ? fill_attr : frame_attr;

      wattron(w, a);
      mvwaddch(w, y, x + 1 + i, on ? '#' : ' ');
      wattroff(w, a);
   }

   wattron(w, frame_attr);
   mvwaddch(w, y, x + 1 + cells, ']');
   wattroff(w, frame_attr);
}

/* Shared renderer for the directory / file coverage rows. */
static void
draw_cov_row(WINDOW *w, int y, const std::string &name,
             int lh, int lf, int fnh, int fnf, bool sel)
{
   const int width = getmaxx(w);
   const chtype sel_a = cv_attr(CVP_SEL);

   if (sel) {
      wattron(w, sel_a);
      mvwhline(w, y, 0, ' ', width);
      wattroff(w, sel_a);
   } else {
      wmove(w, y, 0);
      wclrtoeol(w);
   }

   const int barw = 10;
   const int lpw = 6, lcw = 11, fpw = 6, fcw = 11;

   int x = width;
   x -= fcw; const int col_fc = x; x -= 1;
   x -= fpw; const int col_fp = x; x -= 2;
   x -= lcw; const int col_lc = x; x -= 1;
   x -= lpw; const int col_lp = x; x -= 1;
   x -= barw + 2; const int col_bar = x; x -= 1;
   const int name_w = x;

   if (name_w < 8) {
      /* Too narrow for the full layout: name + line% only. */
      put_field(w, y, 0, std::max(0, width - 7), name, sel ? sel_a : 0, false);
      put_field(w, y, width - 6, 6, fmt_pct(cov_rate(lh, lf), lf),
                sel ? sel_a : bucket_attr(cov_bucket(lh, lf)), true);
      return;
   }

   put_field(w, y, 0, name_w, name, sel ? sel_a : 0, false);
   draw_bar(w, y, col_bar, barw, cov_rate(lh, lf), cov_bucket(lh, lf), sel);

   const chtype la = sel ? sel_a : bucket_attr(cov_bucket(lh, lf));
   const chtype fa = sel ? sel_a : bucket_attr(cov_bucket(fnh, fnf));

   put_field(w, y, col_lp, lpw, fmt_pct(cov_rate(lh, lf), lf), la, true);
   put_field(w, y, col_lc, lcw, fmt_count(lh, lf), la, true);
   put_field(w, y, col_fp, fpw, fmt_pct(cov_rate(fnh, fnf), fnf), fa, true);
   put_field(w, y, col_fc, fcw, fmt_count(fnh, fnf), fa, true);
}

/* ---- views ----------------------------------------------------------- */

void
app::draw_dir_row(WINDOW *w, int y, int idx, int hoff, bool sel)
{
   (void)hoff;
   const dir_cov &d = m.dirs[rows[idx]];
   const std::string name = d.path.empty() ? "." : d.path;
   draw_cov_row(w, y, name, d.lh, d.lf, d.fnh, d.fnf, sel);
}

void
app::draw_file_row(WINDOW *w, int y, int idx, int hoff, bool sel)
{
   (void)hoff;
   const file_cov &f = m.files[rows[idx]];
   draw_cov_row(w, y, f.name, f.lh, f.lf, f.fnh, f.fnf, sel);
}

void
app::draw_source_row(WINDOW *w, int y, int idx, int hoff, bool sel)
{
   const file_cov &f = m.files[cur().file_idx];
   const source_file &src = source_for(cur().file_idx);
   const int width = getmaxx(w);
   const int lineno = idx + 1;

   auto it = f.line_hits.find(lineno);
   line_state st = line_state::none;
   long long hits = -1;

   if (it != f.line_hits.end()) {
      hits = it->second;
      st = hits > 0 ? line_state::covered : line_state::uncovered;
   }

   char gut[32];

   if (hits >= 0)
      snprintf(gut, sizeof(gut), "%6d %9lld :", lineno, hits);
   else
      snprintf(gut, sizeof(gut), "%6d %9s :", lineno, "");

   const std::string text =
      (src.loaded && idx < static_cast<int>(src.lines.size()))
         ? src.lines[idx] : std::string();

   const int gw = static_cast<int>(strlen(gut));

   chtype gutter_a, text_a;

   if (sel) {
      gutter_a = text_a = cv_attr(CVP_SEL);
   } else if (st == line_state::covered) {
      gutter_a = cv_attr(CVP_COVERED);
      text_a = 0;
   } else if (st == line_state::uncovered) {
      gutter_a = text_a = cv_attr(CVP_UNCOVERED);
   } else {
      gutter_a = cv_attr(CVP_DIM);
      text_a = 0;
   }

   if (sel) {
      wattron(w, cv_attr(CVP_SEL));
      mvwhline(w, y, 0, ' ', width);
      wattroff(w, cv_attr(CVP_SEL));
   } else {
      wmove(w, y, 0);
      wclrtoeol(w);
   }

   put_field(w, y, 0, gw, gut, gutter_a, false);

   /* Source text, horizontally scrolled by hoff. */
   const int tx = gw + 1;
   std::string shown;

   if (hoff < static_cast<int>(text.size()))
      shown = text.substr(hoff);

   put_field(w, y, tx, std::max(0, width - tx), shown, text_a, false);
}

void
app::draw_func_row(WINDOW *w, int y, int idx, int hoff, bool sel)
{
   (void)hoff;
   const file_cov &f = m.files[cur().file_idx];
   const func_cov &fn = f.funcs[rows[idx]];
   const int width = getmaxx(w);
   const long long hits = fn.hits < 0 ? 0 : fn.hits;

   const chtype sel_a = cv_attr(CVP_SEL);

   if (sel) {
      wattron(w, sel_a);
      mvwhline(w, y, 0, ' ', width);
      wattroff(w, sel_a);
   } else {
      wmove(w, y, 0);
      wclrtoeol(w);
   }

   char cnt[24];
   snprintf(cnt, sizeof(cnt), "%lld", hits);
   const int cw = 12;

   put_field(w, y, 0, std::max(0, width - cw - 1), fn.name,
             sel ? sel_a : 0, false);

   const chtype ca = sel ? sel_a
                         : cv_attr(hits > 0 ? CVP_FNHI : CVP_FNLO);
   put_field(w, y, width - cw, cw, cnt, ca, true);
}

void
app::draw_row(WINDOW *w, int y, int idx, int hoff, bool sel)
{
   switch (cur().kind) {
      case view_kind::dir_list:  draw_dir_row(w, y, idx, hoff, sel); break;
      case view_kind::file_list: draw_file_row(w, y, idx, hoff, sel); break;
      case view_kind::source:    draw_source_row(w, y, idx, hoff, sel); break;
      case view_kind::func_list: draw_func_row(w, y, idx, hoff, sel); break;
   }
}

/* ---- chrome ---------------------------------------------------------- */

void
app::draw_header()
{
   const int width = getmaxx(stdscr);
   const chtype t = cv_attr(CVP_TITLE);

   /* Breadcrumb + per-view totals. */
   std::string crumb = "top level";
   int lh = m.lh, lf = m.lf, fnh = m.fnh, fnf = m.fnf;

   const frame &c = cur();

   if (c.kind == view_kind::file_list) {
      const dir_cov &d = m.dirs[c.dir_idx];
      crumb += " / " + (d.path.empty() ? "." : d.path);
      lh = d.lh; lf = d.lf; fnh = d.fnh; fnf = d.fnf;
   } else if (c.kind == view_kind::source ||
              c.kind == view_kind::func_list) {
      const file_cov &f = m.files[c.file_idx];
      crumb += " / " + f.rel_path;
      crumb += c.kind == view_kind::source ? "  [source]" : "  [functions]";
      lh = f.lh; lf = f.lf; fnh = f.fnh; fnf = f.fnf;
   }

   wattron(stdscr, t);
   mvwhline(stdscr, 0, 0, ' ', width);
   std::string title = "LCOV coverage  -  " + crumb;
   mvwaddnstr(stdscr, 0, 1, title.c_str(), width - 2);

   /* Right-aligned generation date (the report's "Date:" field). */
   const int dl = static_cast<int>(m.date.size());

   if (dl > 0 && width - dl - 2 > static_cast<int>(title.size()) + 2)
      mvwaddstr(stdscr, 0, width - dl - 1, m.date.c_str());

   wattroff(stdscr, t);

   wmove(stdscr, 1, 0);
   wclrtoeol(stdscr);
   mvwprintw(stdscr, 1, 1, "Lines: ");
   wattron(stdscr, bucket_attr(cov_bucket(lh, lf)));
   wprintw(stdscr, "%d/%d (%s)", lh, lf, fmt_pct(cov_rate(lh, lf), lf).c_str());
   wattroff(stdscr, bucket_attr(cov_bucket(lh, lf)));
   wprintw(stdscr, "   Functions: ");
   wattron(stdscr, bucket_attr(cov_bucket(fnh, fnf)));
   wprintw(stdscr, "%d/%d (%s)", fnh, fnf,
           fmt_pct(cov_rate(fnh, fnf), fnf).c_str());
   wattroff(stdscr, bucket_attr(cov_bucket(fnh, fnf)));

   draw_colhdr();
   wnoutrefresh(stdscr);
}

void
app::draw_colhdr()
{
   const int width = getmaxx(stdscr);
   const chtype base = cv_attr(CVP_DIM) | A_UNDERLINE;
   const int sm = cur().sort_mode;

   /* The header column matching the active sort is shown in bold. */
   const auto col = [&](int mode) -> chtype {
      return mode == sm ? (base | A_BOLD) : base;
   };

   wmove(stdscr, 2, 0);
   wclrtoeol(stdscr);

   switch (cur().kind) {

      case view_kind::dir_list:
      case view_kind::file_list: {
         const char *n =
            cur().kind == view_kind::dir_list ? "Directory" : "Filename";
         put_field(stdscr, 2, 1, 20, n, col(0), false);
         put_field(stdscr, 2, width - 47, 12, "Coverage", base, false);
         put_field(stdscr, 2, width - 30, 11, "Lines", col(1), true);
         put_field(stdscr, 2, width - 12, 11, "Functions", col(2), true);
         break;
      }

      case view_kind::source:
         wattron(stdscr, base);
         mvwaddstr(stdscr, 2, 1, "  Line     Hits   Source");
         wattroff(stdscr, base);
         break;

      case view_kind::func_list:
         put_field(stdscr, 2, 1, 20, "Function", col(0), false);
         put_field(stdscr, 2, width - 13, 12, "Hit count", col(1), true);
         break;
   }
}

void
app::draw_footer()
{
   const int width = getmaxx(stdscr);

   wmove(stdscr, LINES - 1, 0);
   wclrtoeol(stdscr);

   const char *keys =
      cur().kind == view_kind::source
         ? "j/k scroll  h/l pan  Tab funcs  Bksp back  ? help  q quit"
         : "j/k move  Enter open  Tab src/funcs  s sort  Bksp back  ? help";

   mvwaddnstr(stdscr, LINES - 1, 1, keys, width - 2);

   if (sort_modes() > 1) {
      char tag[24];
      snprintf(tag, sizeof(tag), "[sort: %s]", sort_label());
      const int tl = static_cast<int>(strlen(tag));
      if (width - tl - 1 > 0)
         mvwaddstr(stdscr, LINES - 1, width - tl - 1, tag);
   }

   wnoutrefresh(stdscr);
}

void
app::show_help()
{
   static const char *lines[] = {
      " coverage-viewer keys ",
      "",
      " Up / k, Down / j   move selection",
      " PgUp / PgDn        page",
      " g / G              first / last",
      " Enter / Right      open (dir -> files -> source)",
      " Backspace / u      back",
      " Left / h           back (lists), pan left (source)",
      " Right / l          open (lists), pan right (source)",
      " Tab / f            toggle source <-> functions",
      " s                  cycle sort order",
      " ? / q              this help / quit",
      "",
      " press any key to close ",
   };

   const int n = static_cast<int>(sizeof(lines) / sizeof(lines[0]));
   int w = 0;

   for (const char *s : lines)
      w = std::max(w, static_cast<int>(strlen(s)));

   w += 2;
   const int h = n + 2;
   WINDOW *win = newwin(h, w, std::max(0, (LINES - h) / 2),
                        std::max(0, (COLS - w) / 2));

   box(win, 0, 0);

   for (int i = 0; i < n; i++)
      mvwaddnstr(win, i + 1, 1, lines[i], w - 2);

   wnoutrefresh(win);
   doupdate();
   wgetch(win);
   delwin(win);

   /* Repaint the current view without disturbing the scroll position. */
   touchwin(stdscr);
   wnoutrefresh(stdscr);
   sv.redraw();
   doupdate();
}

/* ---- model helpers --------------------------------------------------- */

const source_file &
app::source_for(int file_idx)
{
   auto it = srcs.find(file_idx);

   if (it == srcs.end()) {
      source_file s;
      s.load(m.files[file_idx].abs_path);
      it = srcs.emplace(file_idx, std::move(s)).first;
   }

   return it->second;
}

int
app::source_lines(int file_idx)
{
   const source_file &s = source_for(file_idx);
   int n = s.loaded ? static_cast<int>(s.lines.size()) : 0;

   for (const auto &kv : m.files[file_idx].line_hits)
      n = std::max(n, kv.first);

   return n;
}

void
app::build_rows()
{
   rows.clear();
   const frame &c = cur();
   const int sm = c.sort_mode;

   switch (c.kind) {

      case view_kind::dir_list: {
         for (int i = 0; i < static_cast<int>(m.dirs.size()); i++)
            rows.push_back(i);

         std::sort(rows.begin(), rows.end(), [&](int a, int b) {
            const dir_cov &x = m.dirs[a], &y = m.dirs[b];
            if (sm == 1 && x.lf && y.lf)
               return cov_rate(x.lh, x.lf) < cov_rate(y.lh, y.lf);
            if (sm == 2 && x.fnf && y.fnf)
               return cov_rate(x.fnh, x.fnf) < cov_rate(y.fnh, y.fnf);
            return x.path < y.path;
         });
         break;
      }

      case view_kind::file_list: {
         rows = m.dirs[c.dir_idx].files;

         std::sort(rows.begin(), rows.end(), [&](int a, int b) {
            const file_cov &x = m.files[a], &y = m.files[b];
            if (sm == 1)
               return cov_rate(x.lh, x.lf) < cov_rate(y.lh, y.lf);
            if (sm == 2)
               return cov_rate(x.fnh, x.fnf) < cov_rate(y.fnh, y.fnf);
            return x.name < y.name;
         });
         break;
      }

      case view_kind::source: {
         const int n = source_lines(c.file_idx);
         for (int i = 0; i < n; i++)
            rows.push_back(i);
         break;
      }

      case view_kind::func_list: {
         const file_cov &f = m.files[c.file_idx];
         for (int i = 0; i < static_cast<int>(f.funcs.size()); i++)
            rows.push_back(i);

         std::sort(rows.begin(), rows.end(), [&](int a, int b) {
            if (sm == 1)
               return f.funcs[a].hits < f.funcs[b].hits;
            return f.funcs[a].name < f.funcs[b].name;
         });
         break;
      }
   }
}

int
app::sort_modes() const
{
   switch (stack.back().kind) {
      case view_kind::dir_list:
      case view_kind::file_list: return 3;   /* name, line%, func% */
      case view_kind::func_list: return 2;   /* name, hit count */
      case view_kind::source:    return 1;   /* no sorting */
   }

   return 1;
}

const char *
app::sort_label() const
{
   const frame &c = stack.back();

   if (c.kind == view_kind::func_list)
      return c.sort_mode == 1 ? "hits" : "name";

   switch (c.sort_mode) {
      case 1: return "line%";
      case 2: return "func%";
      default: return "name";
   }
}

void
app::cycle_sort()
{
   const int n = sort_modes();

   if (n <= 1)
      return;

   cur().sort_mode = (cur().sort_mode + 1) % n;
   cur().sel = 0;
   cur().top = 0;
   build_rows();
   sv.set_rows(static_cast<int>(rows.size()));
   sv.restore(0, 0, sv.h_off());
   draw_header();
   draw_footer();
   doupdate();
}

/* ---- navigation ------------------------------------------------------ */

void
app::layout()
{
   if (body)
      delwin(body);

   const int h = std::max(1, LINES - HDR_LINES - FTR_LINES);
   body = newwin(h, COLS, HDR_LINES, 0);
   sv.init(body, [this](WINDOW *w, int y, int i, int ho, bool s) {
      draw_row(w, y, i, ho, s);
   });
}

void
app::enter_view()
{
   build_rows();
   werase(stdscr);
   draw_header();
   draw_footer();

   sv.set_rows(static_cast<int>(rows.size()));
   sv.restore(cur().top, cur().sel, cur().hoff);
   doupdate();
}

void
app::push(view_kind k, int dir_idx, int file_idx)
{
   cur().top = sv.top();
   cur().sel = sv.selected();
   cur().hoff = sv.h_off();

   frame f;
   f.kind = k;
   f.dir_idx = dir_idx;
   f.file_idx = file_idx;
   stack.push_back(f);
   enter_view();
}

void
app::back()
{
   if (stack.size() <= 1)
      return;

   stack.pop_back();
   enter_view();
}

void
app::toggle_source_funcs()
{
   frame &c = cur();

   if (c.kind == view_kind::source)
      c.kind = view_kind::func_list;
   else if (c.kind == view_kind::func_list)
      c.kind = view_kind::source;
   else
      return;

   c.top = c.sel = c.hoff = 0;
   enter_view();
}

void
app::on_enter()
{
   const frame &c = cur();

   if (c.kind == view_kind::dir_list) {

      push(view_kind::file_list, rows[sv.selected()], -1);

   } else if (c.kind == view_kind::file_list) {

      push(view_kind::source, c.dir_idx, rows[sv.selected()]);

   } else if (c.kind == view_kind::func_list) {

      const file_cov &f = m.files[c.file_idx];
      const int line = f.funcs[rows[sv.selected()]].line;

      cur().kind = view_kind::source;
      cur().top = cur().sel = cur().hoff = 0;
      enter_view();
      sv.goto_index(line > 0 ? line - 1 : 0);
      doupdate();
   }
}

void
app::handle_key(int ch)
{
   const bool in_source = cur().kind == view_kind::source;

   switch (ch) {

      case KEY_DOWN: case 'j': sv.move_sel(1); doupdate(); break;
      case KEY_UP:   case 'k': sv.move_sel(-1); doupdate(); break;
      case KEY_NPAGE: sv.page(1); doupdate(); break;
      case KEY_PPAGE: sv.page(-1); doupdate(); break;
      case 'g': case KEY_HOME: sv.to_first(); doupdate(); break;
      case 'G': case KEY_END:  sv.to_last(); doupdate(); break;

      case '\n': case '\r': case KEY_ENTER:
         on_enter();
         break;

      case '\t': case 'f':
         toggle_source_funcs();
         break;

      case 's':
         cycle_sort();
         break;

      case '?':
         show_help();
         break;

      case KEY_BACKSPACE: case 127: case 'u':
         back();
         break;

      case KEY_RIGHT: case 'l':
         if (in_source) {
            sv.h_scroll(8);
            doupdate();
         } else {
            on_enter();
         }
         break;

      case KEY_LEFT: case 'h':
         if (in_source) {
            sv.h_scroll(-8);
            doupdate();
         } else {
            back();
         }
         break;

      case KEY_RESIZE:
         layout();
         enter_view();
         break;

      default:
         break;
   }
}

void
app::run()
{
   initscr();
   cbreak();
   noecho();
   keypad(stdscr, TRUE);
   curs_set(0);
   colors_init();

   frame root;
   root.kind = view_kind::dir_list;
   stack.push_back(root);

   layout();
   enter_view();

   int ch;

   while ((ch = getch()) != 'q' && ch != 'Q')
      handle_key(ch);

   if (body)
      delwin(body);

   endwin();
}
