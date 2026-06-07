# Implementation plan — `coverage-viewer` TUI

Parity target: `docs/plans/coverage-viewer-feature-spec.md`. Goal: an ncurses
TUI that is functionally equivalent to the LCOV genhtml report, written
in C++17, linking only `host_ncurses`, sources under
`tools/coverage-viewer/`, built by default.

## 1. Input: parse `coverage.info`, not the HTML

The tool reads the **LCOV tracefile** (`coverage.info`) — the same input
genhtml consumes — plus the source files named by `SF:`. It does **not**
parse `build/coverage_html/*.html`.

Rationale: the tracefile is a tiny, line-oriented, stable format that
carries exactly the data the report is derived from (line/function hit
counts, totals). Parsing it (vs. scraping 942 generated HTML files) is
simpler, robust, and means the viewer works straight after
`run_all_tests --coverage` even without `--html`. The HTML report
remains the *parity spec*; the tracefile is the *data source*.

CLI: `coverage-viewer [PATH]` where PATH is a `coverage.info` file
(default: `./coverage.info`, falling back to `build/coverage.info`).
Source code is read from the absolute `SF:` paths; a file that can't be
opened still shows its line/function counts, with the source pane noting
"source unavailable".

## 2. No-full-redraw rendering engine (hard requirement)

**Forbidden:** the `render(state)` / `UI = f(data)` pattern that rebuilds
the whole screen (or the whole viewport) every frame. That includes
ncurses **pads + `prefresh`** (re-copies the entire visible region into
the virtual screen each refresh) — rejected.

**Design:** an incremental `ScrollView` widget over a real `WINDOW` with
`scrollok(win, TRUE)` + `idlok(win, TRUE)`, so ncurses emits the
terminal's native scroll/insert-delete-line for `wscrl()`:

- **Static chrome** — the header block and footer are their own windows,
  drawn **once** when a view is entered and rewritten only when their
  values actually change (i.e. on view switch). Never touched during
  scrolling.
- **Per-line scroll** (`j`/`k`/arrows past the edge): `wscrl(win, ±1)`
  shifts the body by one line using the terminal's scroll region; then
  **only the single newly-exposed row** is drawn. One scroll + one row.
- **Selection move within the page**: redraw **only the two affected
  rows** — repaint the old row in normal attrs, the new row highlighted.
- **Page up/down**: `wscrl(win, ±page)` then draw the newly-exposed page
  (inherently one body's worth of rows; header/footer untouched).
- **Jump (home/end/goto-line, follow-link)**: redraw the body once.
- **Horizontal scroll** (source view, long lines): terminals have no
  hardware column scroll, so left/right redraws **only the body rows** at
  the new column offset — incremental (body-only, on h-move), never the
  whole screen. This is the best ncurses offers for columns; documented
  as such.
- **Resize (`KEY_RESIZE`)**: recompute layout and do one full repaint of
  the current view (a deliberate, rare event — not per-frame).

`ScrollView` owns: the body `WINDOW`, `top` (first visible model row),
`sel` (highlighted row), `h_off` (horizontal offset), and a pointer to a
**row provider** (the current view). It calls back
`provider.draw_row(win, screen_y, model_index, h_off, highlighted)` to
paint exactly one row. The widget never asks the provider to render
anything but the rows it needs.

`wnoutrefresh()` on the changed window(s) + a single `doupdate()` per
keystroke flush the minimal delta.

## 3. Colors (`colors.{h,cpp}`)

`init_pair` for: `COVERED`, `UNCOVERED`, `LO`, `MED`, `HI`, `FN_HI`,
`FN_LO`, `TITLE`, `HEADER`, `BAR_*`, `SELECTION`. Map to the spec's
palette where the terminal supports it (`has_colors()`,
`COLORS >= 8/256`); degrade to `A_REVERSE`/`A_BOLD`/`A_DIM` on mono
terminals. Selection highlight = a dedicated pair (or `A_REVERSE`)
composed over the row's base attr.

## 4. Module layout (`tools/coverage-viewer/`)

| File | Responsibility |
|------|----------------|
| `lcov_parser.{h,cpp}` | parse `coverage.info` → `FileCov` records (per-file line map, function list, LF/LH/FNF/FNH) |
| `model.{h,cpp}` | aggregate into the flat directory list; rate%, Lo/Med/Hi bucket, sorting comparators |
| `source_file.{h,cpp}` | load a source file, expand tabs (width 8), keep line spans |
| `colors.{h,cpp}` | color-pair setup + degrade |
| `scrollview.{h,cpp}` | the incremental scrolling widget (§2) — the heart of the no-redraw rule |
| `views.{h,cpp}` | the 4 views (row providers + key handling): `DirView`, `FileView`, `SourceView`, `FuncView` |
| `app.{h,cpp}` | curses init/teardown, navigation stack, the input loop, resize |
| `main.cpp` | CLI parsing, wiring |

**Navigation stack**: entries `(view kind, target id, saved top/sel/h_off)`
so going *back* restores the exact scroll position; `source⇄functions`
swaps the leaf view in place.

## 5. View rendering details (parity with the spec)

- **Header window** (drawn once/view): title line; `Current view:`
  breadcrumb; `Test`/`Date`; the Lines and Functions Hit/Total/% with the
  `%` cell in Lo/Med/Hi color.
- **DirView / FileView body row**: `name | [bar] | line% | line cnt |
  func% | func cnt`. Bar = fixed width (e.g. 16) of a block glyph in the
  rate color + blanks. `%`/count cells colored. Name truncated with `…`
  if too wide (lists don't h-scroll). Footer shows the LCOV version +
  a one-line key hint.
- **SourceView body row**: `<lineno> │ <hit-or-blank> │ <src>` with the
  whole row tinted covered/uncovered/none. `h_off` scrolls the source
  text horizontally.
- **FuncView body row**: `name | hit count`, count cell colored by
  zero/nonzero; Enter jumps to `SourceView` at the function's line.
- **Sorting**: a key cycles the active sort (`{name, line%, func%}` for
  lists, `{name, count}` for functions); re-sort the model, reset
  `top/sel`, redraw the body once (a deliberate action).

## 6. Proposed key map (reaches every spec destination)

```
Up/k, Down/j          move selection (incremental)
PgUp/PgDn, Ctrl-B/F   page
Home/g, End/G         jump to first/last
Left/h, Right/l       horizontal scroll (source view)
Enter/Right           descend (dir→files→source; func→source@line)
Backspace/Left/u      back (pop nav stack)
Tab / f               source ⇄ functions (in a file)
s                     cycle sort order
q                     quit
?                     help overlay
```

(Final bindings confirmed during implementation; the spec's §7 map is
the contract.)

## 7. Build integration

- New `tools/coverage-viewer/CMakeLists.txt`; a host C++17 executable
  `coverage-viewer` emitted at the build root (like `gtests`), built by
  the default `make` (added to `ALL`).
- Link only `host_ncurses`: include `${HOST_DIR_DISTRO}/ncurses/${VER_NCURSES}/install/include`
  (and `.../include/ncursesw`); link `.../install/lib/libncursesw.a` +
  `libtinfo.a`. `VER_NCURSES` comes from `other/pkg_versions` (already
  parsed into CMake; confirm the var name at impl time).
- Configure-time guard: if that install tree is absent, skip the target
  with a `message(STATUS ...)` pointing at `build_toolchain` — mirrors
  the `gtests`/`GTEST_INSTALL_DIR` pattern. In practice `host_ncurses` is
  a **default** package (`scripts/pkgmgr/ncurses.rb`, `default: true`),
  so a normal toolchain has it; **no pkgmgr change is required** (the
  spec's "make it default" is already satisfied — to be confirmed with
  `build_toolchain -l`).
- Style: the tool is host code, but follow the kernel C/C++ conventions
  in `CLAUDE.md` (3-space indent, 80 cols, snake_case, brace rules) and
  run `style_check` on every file.

## 8. Commit plan (fine-grained, each compiles & is self-contained)

1. **CMake skeleton** — `tools/coverage-viewer/` builds a minimal
   ncurses host tool (init/teardown + "hello") linking `host_ncurses`,
   built by default. Verify it links and runs on this host.
2. **lcov parser + model** — parse `coverage.info`, build the flat dir
   list + per-file data; a tiny `--dump` debug mode to verify totals
   match the HTML header (e.g. 64.3 % lines / 78.6 % funcs).
3. **colors + ScrollView** — the incremental widget with a scratch
   provider; manually verify hardware scroll (only new row drawn) e.g.
   via `infocmp`/a slow-terminal check.
4. **DirView + app/nav loop** — View A with header/footer, selection,
   scrolling, quit/resize.
5. **FileView** — descend dir→files, back restores position.
6. **SourceView** — line/hit/source, covered/uncovered/none colors,
   horizontal scroll.
7. **FuncView + source⇄functions** — function list, jump-to-line.
8. **Sorting** — cycle sorts in all list views.
9. **Polish & parity sweep** — breadcrumbs, help overlay, tab expansion,
   empty/huge files, missing source, narrow terminals; tick every box in
   spec §8.
10. **Docs** — `tools/coverage-viewer/README` + a pointer from
    `docs/coverage.md` ("prefer the TUI: `coverage-viewer`").

## 9. Verification (parity, per the porting workflow)

- Drive each view; for a handful of files compare the viewer's numbers
  and per-line covered/uncovered classification against the corresponding
  `*.gcov.html` / `index.html` — they must match exactly (same data
  source).
- Confirm grand totals equal the top `index.html` header.
- Walk spec §8 as a checklist; the gap list must reach zero before
  "done".
- Demonstrate native scrolling: scrolling a long source file emits a
  scroll + one line (not a screen repaint) — verify with a terminal that
  shows redraw, or by reasoning over the emitted ncurses ops.

## 10. Open decisions to confirm before coding

1. **Input = `coverage.info`** (not HTML scraping) — see §1. Strong
   recommendation; flagging in case you expected HTML parsing.
2. **Output location / name**: `build/coverage-viewer`, built by default
   `make` (vs. an opt-in target). Plan assumes default + build-root.
3. **Key map** (§6) — sensible defaults; open to your preferences.
4. **Tab width 8** for source expansion (genhtml default).
