# Moving Tilck from 3-space to 4-space indentation: plan

Status: agreed, 2026-11-26. Nothing has been converted yet. The numbers
below were measured on master at 79199d4. The decisions are in section 4.

## 1. Scope

| Language            | Files  | Lines   | Today       | Action                     |
|---------------------|-------:|--------:|-------------|----------------------------|
| C (`.c`, `.h`)      | ~490   | ~111k   | 3           | convert                    |
| style_check fixtures| 94     | small   | 3           | convert with the tool's tests |
| C++ (`.cpp`, `.hpp`)| 50     | 12.5k   | 3           | convert                    |
| Python              | 100    | 18.5k   | 3           | convert                    |
| shell               | 45     | 6.0k    | 3           | convert                    |
| CMake               | 31     | 5.8k    | 3           | convert                    |
| assembly (`.S`)     | 27     | 3.1k    | 3           | convert                    |
| linker scripts      | 4      | 0.4k    | 3           | convert                    |
| small configs/templates | ~8 | ~0.6k   | 3           | convert (`.vscode/*.json`, `.nvim.lua`, `run_config.in`, `.its`) |
| docs                | 16     | -       | 3           | every mention of the indentation, and every code sample |

Left alone:

- **Ruby** (195 files): 2 spaces, Ruby's own convention.
- **YAML** workflows: 2 spaces.
- **`scripts/patches/*.diff`**: patches to upstream sources, in upstream's
  indentation.
- **Third-party headers**: `include/3rd_party/*`,
  `include/system_headers/multiboot.h`.
- **Interactive test data**: `tests/interactive/vim_text_files/*` is opened
  in vim and compared with `tests/interactive/expected/*`; any byte changed
  breaks those screens.
- **Board configs**: `other/busybox.config`, `other/bsp/*/u-boot.config`.
- **Already 4 spaces**: `scripts/dev/configurator/*.py`.

Every document that mentions the indentation is fixed, `docs/plans/`
included. Four places say "3": `CLAUDE.md` (the rule, and "+3" for Style
2), `docs/contributing.md`, the README's FAQ and
`docs/plans/coverage-viewer-plan.md` ("3-space indent").
`docs/annotated-commit-history.txt` does not mention it.

The code samples in 3-space style are converted with them, in 15 files:
`CLAUDE.md`, `docs/contributing.md`, `docs/atomics.md`, `docs/scheduler.md`,
`docs/debugging.md`, `docs/package_manager.md`, `docs/ancient_commits.md`,
six plans (`interactive-tests-qmp.md`, `realtime-roadmap.md`,
`research/abi-glibc.md`, `toolchain5.md`, `unit-tests-in-run-all-tests.md`,
`pkgmgr-correctness-proof.md`), and style_check's
`docs/preferences-notes.md` and `docs/rule-classification.md`.

## 2. What was measured

All of this used a disposable prototype converter and a scratch worktree.
Nothing was committed.

**The mechanical part is small and safe.**

- Over the 586 C/C++ files, only leading whitespace (and the spacing
  before a trailing `\`) changed, on every line. 508 files and 69,730 lines
  change.
- 82,974 lines are plain indent levels (×4/3). 15,721 are aligned
  continuations that shift with the line they align to.

**The real work is the 80-column limit.**

| Files                | Over 80 today      | After the reindent |
|----------------------|-------------------:|-------------------:|
| C/H                  | 71 (all in the PCI tables style_check ignores) | 505 |
| C++                  | 0                  | 33                 |
| Python (estimate)    | 12                 | ~41                |
| shell (estimate)     | 14                 | ~41                |
| CMake (estimate)     | 9                  | ~34                |
| asm (estimate)       | 0                  | ~12                |

- **How far over:** of the ~434 new C/H long lines, 415 are over by 1-3
  columns, 13 by 4-8 and 1 by 9-16.
- **Where:** kernel 207, modules 74, tests 48, include 38, boot 31,
  userapps 15, scripts 13, common 3.
- **What kind:** many are block comments to reflow; the rest is deeply
  nested code.

**style_check sees nothing else move.** It ran with `--gradient` on the
380 C files it can parse here. The only change was `cols_80` (+116); every
other rule, hard and soft, reported identical counts before and after.

**What style_check needs to change.** Very little, but it needs one fix
first.

- **The width:** `--indent N` already exists, default 3 (`CheckContext.indent`
  in `rules/base.py`, the CLI default in `__main__.py`).
  - Only two rules read it: `indent_3sp` (tab expansion) and
    `switch_case_indent`.
  - No other rule hard-codes 3. The continuation rules
    (`multiline_call_style`, `function_def_no_style2`, ...) compare positions
    relatively.
- **Tests:** the 94 fixtures are in 3-space style, and `tests/test_rules.py`
  passes `indent=3`.
- **Docs:** `preferences.yaml` and `docs/preferences-notes.md` are not read
  by the code, but they hold 3-space examples and "+3" wording.
- **Prerequisite:** style_check needs `build/compile_db`, which a normal
  build produces. Without it, every structural rule is skipped silently, and
  18 of its 123 self-tests fail.
- **A crash to fix first:** in an Ubuntu 24.04 container with
  `python3-clang` 18, 192 of the 572 C files crash with `Unknown template
  argument kind 280`. Those bindings lack a cursor kind that libclang 18
  returns. A crash takes out the whole command line's output, so a batch
  run silently under-reports.

**style_check is not enough to validate a reindent.** Sampling the
prototype's output found three bugs that no rule caught:

- a trailing comma inside braces (enum values, initializer lists) was taken
  as an unfinished expression, so a closing `};` gained a space;
- a column-0 `#ifdef` after such a line moved to column 1;
- nested initializer closers landed at 13 instead of 12.

So the converter needs its own tests and its own verifier.

**clang-format is not an option.** With the repository's `.clang-format`
it already rewrites 5-20% of lines at width 3. The file's own header says
it is only a rough guide.

**Python conventions**, measured over all the Python in the tree, the
code Claude wrote included (the corpus for the checker is narrower; see
Phase 0):

- one level is 3 spaces;
- a hanging continuation is +1 level (1,874 cases), sometimes +2 (272),
  with the closing bracket back at the statement's indent (672);
- alignment under an open bracket is also used (180);
- 385 lines sit inside multi-line string literals;
- 61 of the 103 files carry `# pylint: disable=bad-indentation` only
  because pylint expects 4 spaces.

**No rule checks the indentation width.** `indent_3sp` only rejects
tabs, and `switch_case_indent` only checks the `case` step. With the
default set to 4, a 3-space file would still pass almost everything, so
the switch needs a rule that checks each line's indent against its
nesting depth.

**Other rules affected by the width.** CLAUDE.md's "blank line after a
header longer than the body's first line" rule can flip when the body moves
one column further than its header. No style_check rule implements it, so
the converter has to report the flips.

## 3. The plan

The switch comes first: once the tools and the rules say 4, every file not
yet converted fails, and the commits that follow fix them, a few whole
files at a time.

### Phase 0: tools and the switch

1. **style_check survives unknown cursor kinds.** Skip a cursor whose kind
   the bindings cannot name, instead of crashing the run, and add a test.
   Without this, agents on a host like this one get partial results.

2. **style_check checks the indentation width.**
   - `indent_3sp` is renamed `indent_spaces`.
   - A new rule, `indent_depth`, checks every line that starts a statement,
     a declaration, a `case` or a closing brace against its nesting depth
     times the width. It is what makes an unconverted file fail, and it
     catches conversion mistakes like the prototype's `};` off by one. It
     applies to C and C++.
   - The default width becomes 4; the 94 fixtures are converted and the
     expected columns in `tests/test_rules.py` updated.

3. **`scripts/dev/claude/reindent`**, a deterministic converter with
   sub-commands per language. It starts from the prototype, with the bugs
   above fixed. Rules:
   - **C/C++:**
     - indent-level lines are scaled ×4/3;
     - a line aligned under an open bracket shifts with the bracket's line;
     - a hanging (Style 2) continuation is rescaled from its statement;
     - a closing-bracket line takes its opener's indent;
     - block-comment continuations shift with the `/*` line;
     - column-0 preprocessor lines stay; indented directives scale;
     - commas inside braces are list separators, not continuations;
     - the trailing backslashes of multi-line macros are realigned.
   - **asm and linker scripts:** the same, without expression continuation.
   - **Python:** tokenize-based.
     - logical-line levels and hanging continuations are scaled ×4/3;
     - aligned continuations shift;
     - multi-line strings are left untouched, except docstrings, which
       shift with their line;
     - the `bad-indentation` pragmas are removed.
   - **shell:**
     - levels are scaled;
     - backslash continuations shift;
     - heredoc bodies and multi-line quoted strings are left untouched.
   - **CMake:** levels are scaled, alignment shifts, and multi-line quoted
     arguments are left untouched.

   `reindent verify <file>` is the gate for every conversion commit
   (section 4). It compares the file with its version before the commit:
   - the non-whitespace characters are identical, except for the two
     allowed additions below;
   - the token stream is identical, so no space removed between two tokens
     merged them;
   - for Python, `ast.dump` is unchanged, with docstrings normalized by
     `inspect.cleandoc`;
   - the only non-whitespace additions are a line-continuation backslash
     before a newline, and a string literal split into two adjacent
     literals (`"..." "..."`).

   `reindent check <file>` is the width check for the languages without a
   checker (shell, CMake, asm, linker scripts). It also lists the lines over
   80 columns, and every header/body blank-line verdict that a reindent
   flipped.

   Golden tests for each language, covering every bug class above.

4. **A Python style checker**, `scripts/dev/claude/py_style_check`.
   - Its rules come only from your own Python: the lines `git blame` gives
     to you before `CLAUDE.md` was introduced (2026-04-04), about 4,090
     lines in 30 files. That is `tests/runners/` (`run_all_tests`,
     `single_test_run`, `run_interactive_test`, `lib/utils.py`,
     `lib/exceptions.py`, `lib/lang_aux.py`, `lib/stdio.py`, `lib/env.py`),
     `tests/interactive/*.py`, `other/gdb_scripts/*`,
     `other/tilck_unstripped-gdb.py`, and `scripts/dev/` (`generate_pci_ids`,
     `count_lines_of_code`, `cmp_build_dirs`, `cmp_funcs_size`).
   - Not in the corpus: anything written after that date (style_check,
     `qmp.py`, most of `detect_kvm.py` and `qemu.py`, `run_unit_test`,
     `merge_compile_commands.py`, `scripts/dev/configurator/`), 16 lines by
     two other contributors, and the vim test data.
   - The inferred rules are written down first, in this directory, for your
     review, before the checker is built. Then the checker checks all the
     Python in the tree.

5. **The switch.**
   - style_check's default of 4 (above) and the Python checker's.
   - The editor configs:
     - `.nvim.lua`: `vim.g.indent_width = 4` (the width neovim indents
       with, as expanded soft tabs), its comment "Tilck uses 3-space
       indentation", and its own Lua body, which is 3-space indented too;
     - `.clang-format`: `IndentWidth`, `ContinuationIndentWidth` and
       `ConstructorInitializerIndentWidth` set to 4;
     - `.vscode/settings.json`: `"editor.tabSize": 4`, and the four
       `.vscode/*.json` files themselves.
   - The rules and docs:
     - CLAUDE.md: the rule ("3 spaces" becomes 4) and its examples;
     - `docs/contributing.md`: "4 spaces", and the link to discussion #88
       removed, since that discussion no longer applies;
     - README.md: the question "Why using 3 spaces as indentation?" and its
       link removed;
     - style_check's `README.md`, `preferences.yaml` and
       `docs/preferences-notes.md`: "+3" becomes "+4";
     - the plans listed in section 1;
     - Python's style written down in `docs/contributing.md`.
   - Every code sample in these documents is converted, with its alignment
     kept as the documents describe it.

### Phase 1: conversion, a few whole files per commit

- **Commits:** whole files only, grouped up to about 1000 changed lines; a
  file larger than that gets its own commit. Never part of a file.
- **Each commit, per file:**
  1. `reindent` converts it;
  2. its long lines and other style failures are fixed, with whitespace
     only (section 4);
  3. `style_check check` (C/C++), `py_style_check` (Python) or
     `reindent check` (the rest) passes;
  4. `reindent verify` passes against the file before the commit.
- **Agents:** Sonnet agents, each on its own set of files in its own
  worktree, making its own commits. The sets are disjoint, so their commits
  go onto exp-work in any order without conflicts. Eight agents, under the
  default guideline of ten.
- **Each agent:** reads the coding-style section of CLAUDE.md and
  `docs/contributing.md`, and fixes long lines with CLAUDE.md's `cols_80`
  ladder, restricted to whitespace: compact forms (`,` for `, `, no spaces
  around `+`/`-` in pointer arithmetic), less alignment padding, `{` on its
  own line, rewrapping in Style 1/Style 2 with paren symmetry, reflowing
  comments, a continuation backslash, a split string literal.
- **What whitespace cannot fix** goes in
  `docs/plans/indent-4-spaces-followups.md`, file and line, with why:
  - a line still over 80 columns;
  - layout that whitespace alone leaves ugly and a rename would fix.

### Phase 2: the end of the series

- Over the whole tree, `style_check check --all`, `py_style_check` and
  `reindent check` pass, except for the follow-up list.
- `.git-blame-ignore-revs` lists every conversion commit. GitHub's blame
  honors it, and `git blame` does with `blame.ignoreRevsFile`.
- You merge exp-work into master with a merge commit.

### Later: the follow-ups

The cases in `docs/plans/indent-4-spaces-followups.md` are fixed one by
one, with the freedom whitespace does not give (a rename, a local, a
shorter comment), and so with testing.

## 4. Decisions

1. Ruby stays at 2 spaces.
2. Every document that mentions the indentation is fixed, plans included.
3. `docs/contributing.md` and the other docs keep their text, with 4 for 3,
   and their examples realigned. The README question and the
   `docs/contributing.md` link to discussion #88 are removed; you will
   note in the discussion that the style has changed.
4. Python gets a checker, with rules inferred from your code only.
5. Commits of whole files, about 1000 changed lines at most unless one file
   is larger, all on exp-work, merged at the end with a merge commit.
6. The switch happens first; unconverted files then fail until their
   commit fixes them.
7. Conversion commits change whitespace only, and need no testing. The
   only non-whitespace changes allowed are a continuation backslash and a
   string literal split into adjacent literals. No renames.
8. What whitespace cannot fix, or leaves ugly, is recorded for later,
   case by case, with testing.
9. style_check's `indent_3sp` is renamed `indent_spaces`.

## 5. Risks

- **In-flight branches** will conflict almost everywhere. Land this when
  exp-work is quiet. Another branch can be brought over by running the
  same, deterministic tool on its tip.
- **Line numbers move** where lines are wrapped, so `ASSERT` messages
  change their `__LINE__`. Harmless.
- **An agent changing code instead of layout** is caught by
  `reindent verify` on every commit.
