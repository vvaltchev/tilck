/* SPDX-License-Identifier: BSD-2-Clause */

#include "app.h"
#include "colors.h"

#include <algorithm>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

/*
 * Screen rows: title band, totals, blank, column header, rule, body...,
 * blank, footer. Side padding is L/R columns kept blank at the edges.
 */
static const int HDR_LINES = 5;
static const int FTR_LINES = 2;
static const int PAD_L = 3;
static const int PAD_R = 3;

/* Unicode building blocks (require a UTF-8 locale; set in app::run). */
static const char *GL_BAR   = "▅";   /* coverage bar: lower 5/8 block */
static const char *GL_RULE  = "─";   /* horizontal rule       */
static const char *GL_SB_UP    = "▲";   /* scrollbar up arrow   */
static const char *GL_SB_DOWN  = "▼";   /* scrollbar down arrow */
static const char *GL_SB_TRACK = "░";   /* scrollbar track      */
static const char *GL_SB_THUMB = "█";   /* scrollbar thumb      */
static const char *GL_VBAR  = "│";   /* gutter separator      */
static const char *GL_CRUMB = "›";   /* breadcrumb chevron    */
static const char *GL_ELL   = "…";   /* ellipsis              */
static const char *GL_DIR   = "";   /* nerd-font folder      */
static const char *GL_FILE  = "";   /* nerd-font file        */

struct row_layout {
   int name_x, name_w;
   int bar_x, bar_w;
   int lpct_x, lcnt_x;
   int fpct_x, fcnt_x;
   int lpct_w = 6, lcnt_w = 11, fpct_w = 6, fcnt_w = 11;
   bool funcs = true;
};

static row_layout
compute_layout(int W)
{
   row_layout c;
   const int gap = 3, bar_min = 10;
   const int avail = W - PAD_L - PAD_R;
   int metrics_w =
      c.lpct_w + 1 + c.lcnt_w + gap + c.fpct_w + 1 + c.fcnt_w;

   c.name_w = std::min(46, avail - metrics_w - bar_min - 2);

   if (c.name_w < 12) {
      c.funcs = false;
      metrics_w = c.lpct_w + 1 + c.lcnt_w;
      c.name_w = std::min(46, avail - metrics_w - bar_min - 2);
   }

   if (c.name_w < 6)
      c.name_w = std::max(4, avail - metrics_w - 6);

   c.name_x = PAD_L;
   c.bar_x = c.name_x + c.name_w + 2;

   const int metrics_x = W - PAD_R - metrics_w;
   c.lpct_x = metrics_x;
   c.lcnt_x = c.lpct_x + c.lpct_w + 1;
   c.fpct_x = c.funcs ? c.lcnt_x + c.lcnt_w + gap : -1;
   c.fcnt_x = c.funcs ? c.fpct_x + c.fpct_w + 1 : -1;

   c.bar_w = metrics_x - 2 - c.bar_x;
   if (c.bar_w < 4)
      c.bar_w = 4;

   return c;
}

/* ---- small drawing helpers ------------------------------------------- */

static std::string
repeat(const char *glyph, int n)
{
   std::string s;

   for (int i = 0; i < n; i++)
      s += glyph;

   return s;
}

/*
 * Place `s` in the column field [x, x+fw), justified and clipped to fw
 * display columns (ASCII assumed for `s`), with `attr`. Left-justified
 * overflow is truncated with an ellipsis.
 */
static void
put_field(WINDOW *w, int y, int x, int fw, const std::string &s,
          chtype attr, bool right)
{
   if (fw <= 0)
      return;

   std::string out;
   const int len = static_cast<int>(s.size());

   if (right) {
      const std::string t = len > fw ? s.substr(0, fw) : s;
      out = std::string(fw - static_cast<int>(t.size()), ' ') + t;
   } else if (len > fw) {
      out = s.substr(0, fw - 1) + GL_ELL;   /* fw columns total */
   } else {
      out = s + std::string(fw - len, ' ');
   }

   wattron(w, attr);
   mvwaddstr(w, y, x, out.c_str());
   wattroff(w, attr);
}

/* Like put_field (left-justified) but highlights every occurrence of
 * `needle` with `hl`. ASCII text/needle assumed. */
static void
put_field_hl(WINDOW *w, int y, int x, int fw, std::string text,
             chtype base, const std::string &needle, chtype hl)
{
   if (fw <= 0)
      return;

   if (needle.empty()) {
      put_field(w, y, x, fw, text, base, false);
      return;
   }

   if (static_cast<int>(text.size()) > fw)
      text = text.substr(0, fw);

   int col = 0;
   size_t pos = 0;

   const auto emit = [&](const std::string &s, chtype a) {
      if (s.empty())
         return;
      wattron(w, a);
      mvwaddstr(w, y, x + col, s.c_str());
      wattroff(w, a);
      col += static_cast<int>(s.size());
   };

   while (pos < text.size()) {

      const size_t mt = text.find(needle, pos);

      if (mt == std::string::npos) {
         emit(text.substr(pos), base);
         break;
      }

      emit(text.substr(pos, mt - pos), base);
      emit(text.substr(mt, needle.size()), hl);
      pos = mt + needle.size();
   }

   if (col < fw)
      put_field(w, y, x + col, fw - col, "", base, false);
}

static std::string
fmt_pct(double p, int total)
{
   if (total <= 0)
      return "-";

   char buf[16];
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

/*
 * Coverage bar. Both the filled part and the track use the lower 5/8
 * block (GL_BAR): bottom-anchored and 5/8 tall, so it leaves an empty
 * top margin and adjacent rows' bars stay visually detached. The filled
 * part is the rate color, the track a faint grey -- the only difference
 * is color, so fill and track are the same height (no top-edge mismatch).
 */
static void
draw_bar(WINDOW *w, int y, int x, int width, double p, bucket b, bool sel)
{
   if (width <= 0)
      return;

   const double frac = p < 0 ? 0 : (p > 100 ? 100 : p);
   int filled = static_cast<int>(frac / 100.0 * width + 0.5);

   if (filled > width)
      filled = width;

   const chtype fa = sel ? cv_attr(CVP_SEL) : bucket_attr(b);

   if (filled > 0) {
      wattron(w, fa);
      mvwaddstr(w, y, x, repeat(GL_BAR, filled).c_str());
      wattroff(w, fa);
   }

   /*
    * On the selected row the track is left blank -- only the highlight
    * background shows there -- so the white filled blocks stand out
    * cleanly. Off the selection it gets the faint grey track.
    */
   if (filled < width && !sel) {
      const chtype ta = cv_attr(CVP_TRACK);
      wattron(w, ta);
      mvwaddstr(w, y, x + filled, repeat(GL_BAR, width - filled).c_str());
      wattroff(w, ta);
   }
}

/* Clear a body row; for the selected row, lay a subtle inset highlight. */
static void
row_background(WINDOW *w, int y, bool sel)
{
   const int width = getmaxx(w);

   wmove(w, y, 0);
   wclrtoeol(w);

   if (sel) {
      wattron(w, cv_attr(CVP_SEL));
      mvwhline(w, y, 1, ' ', width - 2);
      wattroff(w, cv_attr(CVP_SEL));
   }
}

/* Shared renderer for the directory / file coverage rows. */
static void
draw_cov_row(WINDOW *w, int y, const char *icon, const std::string &name,
             int lh, int lf, int fnh, int fnf, bool sel)
{
   const row_layout c = compute_layout(getmaxx(w));
   const chtype sa = cv_attr(CVP_SEL);

   row_background(w, y, sel);

   if (icon) {
      wattron(w, sel ? sa : cv_attr(CVP_ACCENT));
      mvwaddstr(w, y, c.name_x, icon);
      wattroff(w, sel ? sa : cv_attr(CVP_ACCENT));
   }

   const int nx = c.name_x + (icon ? 2 : 0);
   put_field(w, y, nx, c.name_w - (icon ? 2 : 0), name, sel ? sa : 0, false);

   draw_bar(w, y, c.bar_x, c.bar_w, cov_rate(lh, lf), cov_bucket(lh, lf), sel);

   const chtype la = sel ? sa : bucket_attr(cov_bucket(lh, lf));
   put_field(w, y, c.lpct_x, c.lpct_w, fmt_pct(cov_rate(lh, lf), lf), la, true);
   put_field(w, y, c.lcnt_x, c.lcnt_w, fmt_count(lh, lf), la, true);

   if (c.funcs) {
      const chtype fa = sel ? sa : bucket_attr(cov_bucket(fnh, fnf));
      put_field(w, y, c.fpct_x, c.fpct_w,
                fmt_pct(cov_rate(fnh, fnf), fnf), fa, true);
      put_field(w, y, c.fcnt_x, c.fcnt_w, fmt_count(fnh, fnf), fa, true);
   }
}

/* ---- views ----------------------------------------------------------- */

void
app::draw_dir_row(WINDOW *w, int y, int idx, int hoff, bool sel)
{
   (void)hoff;
   const dir_cov &d = m.dirs[rows[idx]];
   const std::string name = d.path.empty() ? "." : d.path;
   draw_cov_row(w, y, GL_DIR, name, d.lh, d.lf, d.fnh, d.fnf, sel);
}

void
app::draw_file_row(WINDOW *w, int y, int idx, int hoff, bool sel)
{
   (void)hoff;
   const file_cov &f = m.files[rows[idx]];
   draw_cov_row(w, y, GL_FILE, f.name, f.lh, f.lf, f.fnh, f.fnf, sel);
}

void
app::draw_source_row(WINDOW *w, int y, int idx, int hoff, bool sel)
{
   const file_cov &f = m.files[cur().file_idx];
   const source_file &src = source_for(cur().file_idx);
   const int width = getmaxx(w);
   const int lineno = idx + 1;
   const chtype sa = cv_attr(CVP_SEL);

   auto it = f.line_hits.find(lineno);
   line_state st = line_state::none;
   long long hits = -1;

   if (it != f.line_hits.end()) {
      hits = it->second;
      st = hits > 0 ? line_state::covered : line_state::uncovered;
   }

   row_background(w, y, sel);

   const int gx = PAD_L;
   const int lnw = 6, hw = 9;
   const int hits_x = gx + lnw + 1;
   const int sep_x = hits_x + hw + 1;
   const int src_x = sep_x + 2;

   const chtype state_a =
      st == line_state::covered   ? cv_attr(CVP_HI)
    : st == line_state::uncovered ? cv_attr(CVP_LO)
                                  : cv_attr(CVP_DIM);

   char ln[8], cnt[16];
   snprintf(ln, sizeof(ln), "%d", lineno);
   if (hits >= 0)
      snprintf(cnt, sizeof(cnt), "%lld", hits);
   else
      cnt[0] = '\0';

   put_field(w, y, gx, lnw, ln, sel ? sa : cv_attr(CVP_DIM), true);
   put_field(w, y, hits_x, hw, cnt, sel ? sa : state_a, true);

   wattron(w, sel ? sa : cv_attr(CVP_DIM));
   mvwaddstr(w, y, sep_x, GL_VBAR);
   wattroff(w, sel ? sa : cv_attr(CVP_DIM));

   std::string text;
   if (src.loaded && idx < static_cast<int>(src.lines.size())) {
      const std::string &full = src.lines[idx];
      if (hoff < static_cast<int>(full.size()))
         text = full.substr(hoff);
   }

   const chtype text_a =
      sel ? sa : (st == line_state::uncovered ? cv_attr(CVP_LO) : 0);

   put_field_hl(w, y, src_x, std::max(0, width - src_x - PAD_R), text,
                text_a, search, cv_attr(CVP_MATCH));
}

void
app::draw_func_row(WINDOW *w, int y, int idx, int hoff, bool sel)
{
   (void)hoff;
   const file_cov &f = m.files[cur().file_idx];
   const func_cov &fn = f.funcs[rows[idx]];
   const int width = getmaxx(w);
   const long long hits = fn.hits < 0 ? 0 : fn.hits;
   const chtype sa = cv_attr(CVP_SEL);

   row_background(w, y, sel);

   const int cw = 12;
   char cnt[24];
   snprintf(cnt, sizeof(cnt), "%lld", hits);

   put_field(w, y, PAD_L, width - PAD_L - PAD_R - cw - 1, fn.name,
             sel ? sa : 0, false);
   put_field(w, y, width - PAD_R - cw, cw, cnt,
             sel ? sa : cv_attr(hits > 0 ? CVP_HI : CVP_LO), true);
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
   const frame &c = cur();

   std::string crumb = "top level";
   int lh = m.lh, lf = m.lf, fnh = m.fnh, fnf = m.fnf;
   const std::string sep = std::string(" ") + GL_CRUMB + " ";

   if (c.kind == view_kind::file_list) {
      const dir_cov &d = m.dirs[c.dir_idx];
      crumb += sep + (d.path.empty() ? "." : d.path);
      lh = d.lh; lf = d.lf; fnh = d.fnh; fnf = d.fnf;
   } else if (c.kind == view_kind::source ||
              c.kind == view_kind::func_list) {
      const file_cov &f = m.files[c.file_idx];
      crumb += sep + f.rel_path;
      crumb += c.kind == view_kind::source ? "   source" : "   functions";
      lh = f.lh; lf = f.lf; fnh = f.fnh; fnf = f.fnf;
   }

   /* Title band. */
   wattron(stdscr, t);
   mvwhline(stdscr, 0, 0, ' ', width);
   mvwaddnstr(stdscr, 0, PAD_L, crumb.c_str(), width - PAD_L - 2);

   const int dl = static_cast<int>(m.date.size());
   const int crumb_end = PAD_L + static_cast<int>(crumb.size()) + 2;

   if (dl > 0 && width - PAD_R - dl > crumb_end)
      mvwaddstr(stdscr, 0, width - PAD_R - dl, m.date.c_str());
   wattroff(stdscr, t);

   /* Totals. */
   wmove(stdscr, 1, 0);
   wclrtoeol(stdscr);
   mvwaddstr(stdscr, 1, PAD_L, "Lines ");
   wattron(stdscr, bucket_attr(cov_bucket(lh, lf)));
   wprintw(stdscr, "%d/%d %s", lh, lf, fmt_pct(cov_rate(lh, lf), lf).c_str());
   wattroff(stdscr, bucket_attr(cov_bucket(lh, lf)));
   wattron(stdscr, cv_attr(CVP_DIM));
   wprintw(stdscr, "    ");
   wattroff(stdscr, cv_attr(CVP_DIM));
   wprintw(stdscr, "Functions ");
   wattron(stdscr, bucket_attr(cov_bucket(fnh, fnf)));
   wprintw(stdscr, "%d/%d %s", fnh, fnf,
           fmt_pct(cov_rate(fnh, fnf), fnf).c_str());
   wattroff(stdscr, bucket_attr(cov_bucket(fnh, fnf)));

   draw_colhdr();
   wnoutrefresh(stdscr);
}

void
app::draw_colhdr()
{
   const int width = getmaxx(stdscr);
   const int crow = HDR_LINES - 2;
   const int rrow = HDR_LINES - 1;
   const int sm = cur().sort_mode;
   const chtype base = cv_attr(CVP_DIM);

   const auto lbl = [&](int mode) -> chtype {
      return mode == sm ? (cv_attr(CVP_ACCENT) | A_BOLD) : base;
   };

   const auto center = [&](int x0, int w0, const char *s, chtype a) {
      const int l = static_cast<int>(strlen(s));
      put_field(stdscr, crow, x0 + std::max(0, (w0 - l) / 2), l, s, a, false);
   };

   wmove(stdscr, crow, 0);
   wclrtoeol(stdscr);

   const frame &c = cur();

   if (c.kind == view_kind::dir_list || c.kind == view_kind::file_list) {

      const row_layout cl = compute_layout(width);
      const char *n = c.kind == view_kind::dir_list ? "Directory" : "Filename";

      put_field(stdscr, crow, cl.name_x, cl.name_w, n, lbl(0), false);
      center(cl.bar_x, cl.bar_w, "Coverage", base);
      center(cl.lpct_x, cl.lpct_w + 1 + cl.lcnt_w, "Lines", lbl(1));
      if (cl.funcs)
         center(cl.fpct_x, cl.fpct_w + 1 + cl.fcnt_w, "Functions", lbl(2));

   } else if (c.kind == view_kind::source) {

      wattron(stdscr, base);
      mvwaddstr(stdscr, crow, PAD_L, "  Line     Hits");
      mvwaddstr(stdscr, crow, PAD_L + 18, "Source");
      wattroff(stdscr, base);

   } else { /* func_list */

      put_field(stdscr, crow, PAD_L, 20, "Function", lbl(0), false);
      put_field(stdscr, crow, width - PAD_R - 12, 12, "Hit count", lbl(1),
                true);
   }

   /* Rule under the column header. */
   wattron(stdscr, base);
   mvwaddstr(stdscr, rrow, PAD_L,
             repeat(GL_RULE, std::max(0, width - PAD_L - PAD_R)).c_str());
   wattroff(stdscr, base);
}

void
app::draw_footer()
{
   const int width = getmaxx(stdscr);

   wmove(stdscr, LINES - 2, 0);
   wclrtoeol(stdscr);
   wmove(stdscr, LINES - 1, 0);
   wclrtoeol(stdscr);

   if (!status.empty()) {
      wattron(stdscr, cv_attr(CVP_LO));
      mvwaddnstr(stdscr, LINES - 1, PAD_L, status.c_str(), width - 2 * PAD_L);
      wattroff(stdscr, cv_attr(CVP_LO));
      wnoutrefresh(stdscr);
      return;
   }

   const char *keys =
      cur().kind == view_kind::source
         ? "j/k scroll  / ? search  n N next  g goto  < > ends  Tab funcs"
         : "j/k move  Enter open  < > ends  Tab src/funcs  s sort  ? help";

   wattron(stdscr, cv_attr(CVP_DIM));
   mvwaddnstr(stdscr, LINES - 1, PAD_L, keys, width - 2 * PAD_L);
   wattroff(stdscr, cv_attr(CVP_DIM));

   if (sort_modes() > 1) {
      char tag[24];
      snprintf(tag, sizeof(tag), "sort: %s", sort_label());
      const int tl = static_cast<int>(strlen(tag));
      if (width - PAD_R - tl > 0) {
         wattron(stdscr, cv_attr(CVP_ACCENT));
         mvwaddstr(stdscr, LINES - 1, width - PAD_R - tl, tag);
         wattroff(stdscr, cv_attr(CVP_ACCENT));
      }
   }

   wnoutrefresh(stdscr);
}

/*
 * Vertical scrollbar in its own one-column window to the right of the
 * body, so the body's wscrl() never moves it. ▲/▼ arrows, a ░ track and
 * a proportional █ thumb (inspired by azpipes' v2 UI). Repainted via the
 * scroll_view's on-change hook whenever the position changes.
 */
void
app::draw_scrollbar()
{
   if (!sbar)
      return;

   const int h = getmaxy(sbar);

   if (h < 2)
      return;

   const int total = sv.rows();
   const int visible = getmaxy(body);
   const int top = sv.top();
   const chtype dim = cv_attr(CVP_DIM);
   const chtype track = cv_attr(CVP_TRACK);

   wattron(sbar, dim);
   mvwaddstr(sbar, 0, 0, GL_SB_UP);
   mvwaddstr(sbar, h - 1, 0, GL_SB_DOWN);
   wattroff(sbar, dim);

   const int inner = h - 2;

   if (inner <= 0) {
      wnoutrefresh(sbar);
      return;
   }

   int thumb_h = inner, thumb_top = 0;

   if (total > visible && total > 0) {

      thumb_h = inner * visible / total;
      if (thumb_h < 1)
         thumb_h = 1;
      if (thumb_h > inner)
         thumb_h = inner;

      const int max_top = total - visible;
      if (max_top > 0)
         thumb_top = (inner - thumb_h) * top / max_top;
   }

   for (int i = 0; i < inner; i++) {

      const bool on = i >= thumb_top && i < thumb_top + thumb_h;

      wattron(sbar, on ? dim : track);
      mvwaddstr(sbar, 1 + i, 0, on ? GL_SB_THUMB : GL_SB_TRACK);
      wattroff(sbar, on ? dim : track);
   }

   wnoutrefresh(sbar);
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
      case view_kind::file_list: return 3;
      case view_kind::func_list: return 2;
      case view_kind::source:    return 1;
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

void
app::show_help()
{
   static const char *lines[] = {
      " coverage-viewer keys ",
      "",
      " Up / k, Down / j   move selection",
      " PgUp / PgDn        page",
      " < / >              first / last",
      " Enter / Right      open (dir -> files -> source)",
      " Backspace / u      back",
      " Left / h           back (lists), pan left (source)",
      " Right / l          open (lists), pan right (source)",
      " Tab / f            toggle source <-> functions",
      " s                  cycle sort order",
      "",
      " in the source view:",
      " g                  go to line",
      " / , ?              search forward / backward",
      " n / N              next / previous match",
      " Esc                clear search",
      "",
      " ? help (lists)     q quit     any key: close ",
   };

   const int n = static_cast<int>(sizeof(lines) / sizeof(lines[0]));
   int w = 0;

   for (const char *s : lines)
      w = std::max(w, static_cast<int>(strlen(s)));

   w += 2;
   const int h = n + 2;
   WINDOW *win = newwin(h, w, std::max(0, (LINES - h) / 2),
                        std::max(0, (COLS - w) / 2));

   wbkgd(win, cv_attr(CVP_TITLE));
   box(win, 0, 0);

   for (int i = 0; i < n; i++)
      mvwaddnstr(win, i + 1, 1, lines[i], w - 2);

   wnoutrefresh(win);
   doupdate();
   wgetch(win);
   delwin(win);

   touchwin(stdscr);
   wnoutrefresh(stdscr);
   sv.redraw();
   doupdate();
}

/* ---- prompt / search ------------------------------------------------- */

/*
 * A one-line input prompt on the footer row (vi/less style). Returns true
 * with `out` set on Enter, false on ESC. Restores the footer on exit.
 */
bool
app::prompt(const std::string &label, std::string &out)
{
   std::string buf;
   bool ok = false;

   curs_set(1);

   while (true) {

      wmove(stdscr, LINES - 1, 0);
      wclrtoeol(stdscr);
      mvwaddstr(stdscr, LINES - 1, PAD_L, label.c_str());
      mvwaddnstr(stdscr, LINES - 1, PAD_L + static_cast<int>(label.size()),
                 buf.c_str(), COLS - PAD_L - 1);
      wnoutrefresh(stdscr);
      doupdate();

      const int ch = getch();

      if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
         ok = true;
         break;
      }

      if (ch == 27) {
         ok = false;
         break;
      }

      if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
         if (!buf.empty())
            buf.pop_back();
         continue;
      }

      if (ch >= 32 && ch < 127)
         buf += static_cast<char>(ch);
   }

   curs_set(0);
   out = buf;
   draw_footer();
   doupdate();
   return ok;
}

void
app::set_status(const std::string &msg)
{
   status = msg;
   draw_footer();
   doupdate();
}

void
app::goto_line()
{
   if (cur().kind != view_kind::source)
      return;

   std::string s;

   if (!prompt("Go to line: ", s))
      return;

   size_t a = s.find_first_not_of(" \t");
   size_t b = s.find_last_not_of(" \t");

   if (a == std::string::npos) {
      return;   /* empty input: do nothing */
   }

   s = s.substr(a, b - a + 1);
   bool digits = true;

   for (char c : s)
      if (c < '0' || c > '9')
         digits = false;

   if (!digits) {
      set_status("Invalid line number: " + s);
      return;
   }

   const int ln = std::atoi(s.c_str());
   const int last = sv.rows();

   if (ln < 1 || ln > last) {
      set_status("Line out of range (1-" + std::to_string(last) + ")");
      return;
   }

   sv.goto_index(ln - 1);
   doupdate();
}

int
app::find_match_line(int from, int dir)
{
   if (search.empty() || cur().kind != view_kind::source)
      return -1;

   const source_file &src = source_for(cur().file_idx);
   const int n = sv.rows();

   for (int i = from; i >= 0 && i < n; i += dir) {

      if (i < static_cast<int>(src.lines.size()) &&
          src.lines[i].find(search) != std::string::npos)
         return i;
   }

   return -1;
}

void
app::start_search(int dir)
{
   if (cur().kind != view_kind::source)
      return;

   std::string s;

   if (!prompt(dir > 0 ? "/" : "?", s) || s.empty())
      return;

   search = s;
   search_dir = dir;

   const int found = find_match_line(sv.selected(), dir);

   if (found < 0) {
      search.clear();
      sv.redraw();
      set_status("Pattern not found: " + s);
      return;
   }

   sv.goto_index(found);
   sv.redraw();   /* repaint the whole viewport so every match highlights */
   doupdate();
}

void
app::search_move(int sign)
{
   if (search.empty() || cur().kind != view_kind::source)
      return;

   const int d = sign * search_dir;
   const int found = find_match_line(sv.selected() + d, d);

   if (found < 0) {
      set_status("Pattern not found: " + search);
      return;
   }

   sv.goto_index(found);
   sv.redraw();   /* repaint the whole viewport so every match highlights */
   doupdate();
}

void
app::clear_search()
{
   if (search.empty())
      return;

   search.clear();

   if (cur().kind == view_kind::source) {
      sv.redraw();
      doupdate();
   }
}

/* ---- navigation ------------------------------------------------------ */

void
app::layout()
{
   if (body)
      delwin(body);
   if (sbar)
      delwin(sbar);

   const int h = std::max(1, LINES - HDR_LINES - FTR_LINES);
   body = newwin(h, COLS, HDR_LINES, 0);

   /* One-column scrollbar inside the right padding (column COLS-2). */
   sbar = newwin(h, 1, HDR_LINES, std::max(0, COLS - 2));

   sv.init(body, [this](WINDOW *w, int y, int i, int ho, bool s) {
      draw_row(w, y, i, ho, s);
   });
   sv.set_on_change([this] { draw_scrollbar(); });
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
   c.sort_mode = 0;
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
      cur().sort_mode = 0;
      enter_view();
      sv.goto_index(line > 0 ? line - 1 : 0);
      doupdate();
   }
}

void
app::handle_key(int ch)
{
   const bool in_source = cur().kind == view_kind::source;

   /* A transient status message clears on the next keystroke. */
   if (!status.empty()) {
      status.clear();
      draw_footer();
      doupdate();
   }

   switch (ch) {

      case KEY_DOWN: case 'j': sv.move_sel(1); doupdate(); break;
      case KEY_UP:   case 'k': sv.move_sel(-1); doupdate(); break;
      case KEY_NPAGE: sv.page(1); doupdate(); break;
      case KEY_PPAGE: sv.page(-1); doupdate(); break;
      case '<': case KEY_HOME: sv.to_first(); doupdate(); break;
      case '>': case KEY_END:  sv.to_last(); doupdate(); break;

      case '\n': case '\r': case KEY_ENTER:
         on_enter();
         break;

      case '\t': case 'f':
         toggle_source_funcs();
         break;

      case 's':
         cycle_sort();
         break;

      case 'g':
         goto_line();
         break;

      case '/':
         start_search(1);
         break;

      case '?':
         if (in_source)
            start_search(-1);
         else
            show_help();
         break;

      case 'n': search_move(1); break;
      case 'N': search_move(-1); break;

      case 27:   /* ESC */
         clear_search();
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
   setlocale(LC_ALL, "");

   initscr();
   cbreak();
   noecho();
   keypad(stdscr, TRUE);
   curs_set(0);
   set_escdelay(25);   /* make a lone ESC responsive */
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
   if (sbar)
      delwin(sbar);

   endwin();
}
