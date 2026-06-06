# coverage-viewer

A terminal UI for browsing Tilck's code coverage, without leaving the
terminal to open a browser. It is a functional equivalent of the LCOV
`genhtml` HTML report (the full parity catalog is in
`docs/plans/coverage-viewer-feature-spec.md`).

## Building

`coverage-viewer` is a host C++17 tool, built by default with the rest of
the project (`make`), and emitted at the build root as
`<BUILD_DIR>/coverage-viewer`. Its only dependency is the `host_ncurses`
toolchain package, which is installed by default
(`./scripts/build_toolchain`).

## Usage

    <BUILD_DIR>/coverage-viewer [coverage.info]

The argument defaults to `coverage.info`. Produce one with
`run_all_tests --coverage` (see `docs/coverage.md`), then:

    <BUILD_DIR>/coverage-viewer <BUILD_DIR>/coverage.info

`--dump` prints the parsed totals and exits (no UI).

## Views & keys

Four views mirror the HTML report: the directory overview, a per-
directory file list, the per-file source view (line/hit/source with
covered/uncovered coloring), and the per-file function list.

```
Up/k, Down/j     move selection
PgUp/PgDn        page
g / G            first / last
Enter / Right    open (directory -> files -> source)
Backspace / u    back
Left / h         back (lists) / pan left (source)
Right / l        open (lists) / pan right (source)
Tab / f          toggle source <-> functions
s                cycle sort order (name / line% / func% ; name / hits)
?                help
q                quit
```

## Notes

The viewer reads the LCOV tracefile (`coverage.info`) directly — the same
input `genhtml` consumes — plus the source files it references; it does
not parse the generated HTML. Rendering updates incrementally (it uses
the terminal's native scrolling and repaints only the rows that change),
rather than redrawing the whole screen on every keystroke.
