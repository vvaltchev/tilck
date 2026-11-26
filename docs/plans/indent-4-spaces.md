# Moving Tilck from 3-space to 4-space indentation: plan

Status: proposal, 2026-11-26. Nothing has been converted yet. The numbers
below were measured on master at 79199d4.

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
| docs code samples   | ~10    | -       | 3           | update with the rule text  |

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
- **Historical documents**: `docs/plans/*` (except this one) and
  `docs/annotated-commit-history.txt`.

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

**Python conventions**, inferred from the code since there is no lint
config:

- one level is 3 spaces;
- a hanging continuation is +1 level (1,874 cases), sometimes +2 (272),
  with the closing bracket back at the statement's indent (672);
- alignment under an open bracket is also used (180);
- 385 lines sit inside multi-line string literals;
- 61 of the 103 files carry `# pylint: disable=bad-indentation` only
  because pylint expects 4 spaces.

**Other rules affected by the width.** CLAUDE.md's "blank line after a
header longer than the body's first line" rule can flip when the body moves
one column further than its header. No style_check rule implements it, so
the converter has to report the flips.

## 3. The plan

### Phase 0: tooling (no source changes)

1. **style_check survives unknown cursor kinds.** Skip a cursor whose kind
   the bindings cannot name, instead of crashing the run, and add a test.
   Without this, agents on a host like this one get partial results.

2. **`scripts/dev/claude/reindent`**, a deterministic converter with
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

   `reindent verify` checks, per file:
   - only leading whitespace and backslash spacing changed;
   - every indent-level line is exactly ×4/3;
   - every aligned line's first character still sits under the same
     character of the line it aligned to;
   - for Python, `ast.dump` is unchanged, with docstrings normalized by
     `inspect.cleandoc`;
   - it lists every header/body blank-line verdict that flipped.

   `reindent report` lists, per file, the lines over 80 columns and the
   cases it could not decide. That list is the agents' work list.

   Golden tests for each language, covering every bug class above.

3. **Baselines:**
   - `style_check check --all --gradient` per file, at width 3, to compare
     prettiness before and after;
   - a full build of every configuration, for binary identity.

### Phase 1: the mechanical commit (scripted, no agents)

- Run `reindent` over every in-scope file, then `reindent verify`.
- Rebuild every configuration (`./scripts/adv/gen_other_builds`). The
  binaries must be byte-identical: leading whitespace changes no token and
  no line number. That is the proof that this commit changes nothing but
  layout.
- Run the gtests.
- Run `style_check --all --indent 4`. Expect only `cols_80` and the items
  `reindent report` lists.
- The same commit switches style_check's default to 4, renames
  `indent_3sp` to `indent_spaces`, converts its fixtures and updates the
  expected columns in `test_rules.py`. The code and the checker change
  together, so there is never a window where they disagree.

Running the script centrally, not one file per agent, keeps this step
deterministic. It takes seconds, and binary identity can only be checked
over the whole tree.

### Phase 2: the agent pass (Sonnet, in parallel)

**Work list:**
- ~434 C/H and 33 C++ long lines;
- ~130 more in Python, shell, CMake and asm;
- the review items and blank-line flips that `reindent` lists.

**Split:**
- up to 8 batches of disjoint files, by directory, balanced by line count
  (the kernel in 2-3 batches);
- one Sonnet agent per batch, all in one worktree since the files never
  overlap;
- no builds inside agents.

**Each agent:**
1. Read the coding-style section of CLAUDE.md and `docs/contributing.md`.
2. For each file, run `style_check check --indent 4` (C) or
   `reindent report` (other languages).
3. Fix with CLAUDE.md's `cols_80` ladder:
   - padding and the compact forms first;
   - then comment reflow;
   - then wrapping in Style 1/Style 2 with paren symmetry;
   - only then extracting a local.
4. Repeat until there are no violations and no new gradient diagnostics.
5. Run `reindent verify --tokens <file>`. The token stream must equal the
   mechanical commit's, with comments compared word by word, so a reflow
   passes. Any intended token change, such as an extracted local, goes in
   the agent's report for review.

**Review:** one adversarial review agent reads the whole diff against the
mechanical commit, rule by rule.

### Phase 3: integration and verification

- **Builds:** the full matrix (`./scripts/adv/gen_other_builds`), plus the
  riscv64 and x86_64 configurations.
- **Tests:** the gtests, and the system tests on i386 and riscv64
  (`run_all_tests -c`).
- **Style:**
  - `style_check --all`: zero hard violations, and no file's gradient
    worse than its baseline;
  - `reindent verify` over the whole tree.
- **CI:** dispatch `ci-i386`, `ci-riscv64`, `ci-x86_64` and `ci-tc-*` on
  exp-work, then fast-forward master.

### Phase 4: rules, docs and editors (same series)

- **CLAUDE.md:** the rule ("3 spaces" becomes 4) and its examples.
- **`docs/contributing.md`:** the rule. README.md links to the discussion
  "Why using 3 spaces as indentation?" (#88), which needs your wording.
- **style_check docs:** `README.md`, `preferences.yaml` and
  `docs/preferences-notes.md`: "+3" becomes "+4", and the examples are
  converted.
- **Editor configs:**
  - `.clang-format`: `IndentWidth`, `ContinuationIndentWidth` and
    `ConstructorInitializerIndentWidth` set to 4;
  - `.nvim.lua`: `indent_width = 4`;
  - `.vscode/settings.json`: `"editor.tabSize": 4`.
- **Python's style written down:** a section in `docs/contributing.md`.
  Optionally a `pycodestyle`/pylint config, so that Python gets a checker.
- **`.git-blame-ignore-revs`:** the mechanical commit and the fix commits.
  GitHub's blame honors it, and `git blame` does with
  `blame.ignoreRevsFile`.

## 4. Commit series

1. style_check: survive unknown cursor kinds.
2. `reindent` and its tests.
3. The mechanical pass for C, C++, asm and ld, plus style_check's default
   of 4, its fixtures and its tests.
4. Agent fixes for C/C++, one commit per area.
5. Python: the mechanical pass and fixes. Then shell and CMake the same way.
6. Docs and editor configs.
7. `.git-blame-ignore-revs`.

Every commit builds and passes the tests. Commit 3 still has lines over 80
columns, which commit 4 removes.

## 5. Risks

- **In-flight branches** will conflict almost everywhere. Land this when
  exp-work is quiet. Another branch can be brought over by running the
  same, deterministic tool on its tip.
- **Line numbers move** where lines are wrapped, so `ASSERT` messages
  change their `__LINE__`. Harmless.
- **C++ has no style_check coverage.** It relies on `reindent verify` and
  the long-line report.
- **An agent changing code instead of layout** is caught by the token
  check and by the review.

## 6. Decisions for you

1. Ruby and YAML stay as they are?
2. Historical `docs/plans/*` left untouched?
3. The README link and the wording in `docs/contributing.md`.
4. Should Python get a checker (pycodestyle/pylint with a repo config)?
5. One series for every language, or one per language (C first)?
6. Agent count: the default guideline is under 10, and 8 fix agents plus
   one reviewer fits it.
7. Keep the mechanical commit separate (bisectable, provably layout-only)
   or squash it with the fixes?
