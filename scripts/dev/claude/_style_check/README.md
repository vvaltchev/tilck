# style_check -- Tilck coding-style checker (C and Python)

A linter that reports formatting violations against the user's C and
Python coding style, as text or machine-readable JSONL: C through
libclang, Python through the standard library's `tokenize` and `ast`.
Built for Claude to consume so it can apply the style correctly without
having to memorize every nuance.

## Rules

The authoritative list lives in code and is printed by `list-rules`:

```bash
./scripts/dev/claude/style_check list-rules        # one-liner per rule
./scripts/dev/claude/style_check explain <rule_id> # full description
```

Rule modules are under `_style_check/rules/`. Each rule has a matching
`bad_<rule_id>.c` fixture under `_style_check/tests/fixtures/` and a
test method under `_style_check/tests/test_rules.py`.

C++ (`.cpp` / `.hpp`) is intentionally out of scope. The `tests/unit/`
gtests are not yet curated and don't reflect the user's full C++
preferences.

The rule set is grouped roughly by detection layer (see `Architecture`
below):

- **Raw text / tokens** -- file-level layout (`cols_80`, `indent_3sp`,
  `trailing_ws`, `spdx_header`, `pragma_once`, `include_order`,
  `endif_annotation_long_blocks`), token-shape spelling
  (`sizeof_parens`, `hex_literal_lowercase`, `no_void_cast_discard`,
  `else_same_line_as_brace`, `cast_no_asymmetric_form`,
  `while_true_only`, `break_before_operator_forbidden`,
  `blank_line_after_non_final_return`).
- **Structural (libclang)** -- AST-driven rules
  (`static_fn_def_type_own_line`, `void_arglist`,
  `enum_trailing_comma`, `one_stmt_per_line`,
  `fn_body_brace_own_line`, `multiline_call_style`,
  `pointer_asterisk_attached`, `switch_case_indent`,
  `blank_line_after_decl_block`, `non_const_locals_top_of_block`,
  `function_def_no_style2`, `no_packed_case_labels`,
  `per_case_braces_when_locals`, `empty_body_braces`,
  `no_packed_enum_values`).
- **Comments** -- `comment_block_multiline_format`
  (state-machine scanner + raw text).

### Python

A file is Python when its name ends in `.py` or its first line is a
python shebang (`languages.py`). The Python rules (`rules/py_*.py`) come
from the user's own Python only, as `docs/plans/python-style.md` infers
it, and each cites its section there. `cols_80` and `trailing_ws` apply
to both languages.

- **Tokens** -- `py_indent` (one `--indent` level per block, spaces
  only), `py_hanging_bracket` and `py_aligned_bracket` (the two
  multi-line bracket styles of C's calls), `py_backslash_align`, and
  PEP 8's spacing: `py_call_kw_spacing` (`key=value`),
  `py_def_default_spacing`, `py_annotation_spacing` (`name: T`), each
  with its fix for `--diff`.
- **Structural (ast)** -- vertical space: `py_blank_after_header`,
  `py_branch_symmetry`, `py_blank_after_jump`, `py_blank_between_defs`,
  `py_blank_line_runs`; forms: `py_dict_colon_uniform`,
  `py_assert_parens`.
- **Raw text** -- `py_spdx_header`.
- **Gradient** -- `py_harmony`, the model of `harmony_with_neighbors`
  (shared, in `harmony.py`) over Python's functions.

**The vertical-space rules are soft, and their findings are judged, not
applied.** Vertical space is weight (`python-style.md` 3.1): the more
structure a block has inside, the more space may separate it, a light
block cannot carry extra air, and the spacing tells the common path from
the rare one. A script cannot weigh that, so these rules report warnings
whose suggestion says to judge them in context, they carry no fix, and a
finding the code's context justifies stays as it is.

Their tests are snippets in `tests/test_py_rules.py`, plus a whole file
in the style, `tests/fixtures/good_python_style.py`, on which no rule
may report anything.

## Installation

```bash
# Linux
sudo apt install python3-clang libclang-cpp14

# macOS (Homebrew Python is PEP 668-managed, hence --break-system-packages;
# libclang.dylib itself ships with Xcode Command Line Tools)
python3 -m pip install --break-system-packages clang

# FreeBSD
pkg install py311-clang
```

Any version of the bindings works with any libclang: a cursor kind
the bindings are too old to name is invisible to the rules, and
`--parse-only` reports it. Newer bindings (PyPI `clang` >= 19) know
more kinds.

style_check parses with the compile commands of real builds (see
[Parsing](#parsing)): build Tilck first, `./scripts/cmake_run && make`.

## Parsing

A file is parsed only with the compile commands of the configurations
that compile it -- as the main file, or through an `#include` -- never
with flags borrowed from another file. A configuration is a build tree
with a `compile_db/compile_commands.json` (merged by every build). By
default every build tree of the checkout is one: `build/` and each
`build-*/`; `--build-dir DIR` (repeatable) names them instead. A file
meant for riscv64 is parsed with riscv64's commands; a branch only a
build option enables is parsed with the commands of a build that
enables it.

Each parse reproduces the compiler's environment and is checked
against the compiler itself:

- libclang gets the compiler's target (`-dumpmachine`), its system
  include directories and nothing else (`-nostdinc` plus its own
  `-E -v` list), its default `-std`, and the command's flags, minus
  those libclang rejects (it is asked which: GCC-only options such as
  `-maccumulate-outgoing-args`). It also gets `-DCLANGD`, Tilck's
  macro for "a clang-based tool is reading this", as clangd does.
- The compiler runs the same command with `-E -fdirectives-only -dI`.
  Its output tells which files the compilation reads and which `#if`
  branch it takes in each (`conditionals.py`); the answers are cached
  in `<build>/style_check/views.json` until a command or a file
  changes.
- A parse must have **zero libclang errors**, and must take **exactly
  the branches the compiler takes**. Branches that ask which compiler
  reads the code (`__clang__`, `__GNUC__`, `__has_builtin` ...) or
  test `CLANGD` may differ, libclang being clang; there a difference
  that hides code the compiler builds is a warning.
- A file's parses are chosen so that, together, they take every
  branch some compilation takes (usually one parse; a header used by
  the kernel and by userspace may need two). A rule that reads the
  syntax tree runs on each, and what they report alike is kept once.

Nothing falls back to a guess. A file no configuration compiles, a
compiler that fails, a libclang error or a divergence is a `parse`
diagnostic of severity error, and `check` then exits 1. A
`.style.yml` with `parse: false` marks files that cannot be parsed by
design (the CMake templates in `config/`): only the text rules run on
them. So does a `/* style_check: hosts Darwin */` marker in a file's
first lines, on any host but those named (as Python's
`platform.system()` names them): the file is built only there
(`scripts/build_apps/machohack.c`, a Mach-O tool), so no build tree of
another host compiles it.

To prove the parsing works across the whole tree:

```bash
./scripts/dev/claude/style_check check --parse-only --all      # -v lists
                                                                # dead code
```

It checks no style. It parses every file as the rules would, prints
any problem, and ends with a summary: the configurations used, the
parses made, and the `#if` branches that no configuration compiles
(dead code, or code behind a build option none of the build trees
enables: build one that does, and it gets parsed).

## Usage

`style_check --help` prints the top-level summary; each sub-command
has its own `--help` (e.g. `style_check check --help`).

```bash
# Check files (default output: human-readable text)
./scripts/dev/claude/style_check check kernel/poll.c

# Machine-readable JSONL (for Claude to consume)
./scripts/dev/claude/style_check check --json kernel/poll.c

# (Equivalent long form)
./scripts/dev/claude/style_check check --format jsonl kernel/poll.c

# Check files changed since a git ref
./scripts/dev/claude/style_check check --since HEAD~10

# Filter to specific rules
./scripts/dev/claude/style_check check --rule cols_80 --rule sizeof_parens kernel/

# Skip specific rules
./scripts/dev/claude/style_check check --exclude-rule trailing_ws kernel/poll.c

# Check a code base that uses a different indentation width (1-8).
# Default is 3 (Tilck style). Affects the width-sensitive rules:
# tab expansion (indent_3sp), the case-label step (switch_case_indent),
# and Python's block and bracket indentation (py_indent, py_*_bracket).
./scripts/dev/claude/style_check check --indent 4 ../other-project/src/foo.c

# List all rules / explain one
./scripts/dev/claude/style_check list-rules
./scripts/dev/claude/style_check explain multiline_call_style

# Run the unit test suite (synthetic bad fixtures + golden files)
./scripts/dev/claude/style_check test
./scripts/dev/claude/style_check test -v        # verbose
./scripts/dev/claude/style_check test -f        # fail-fast

# Tests + line coverage (HTML report in scripts/dev/claude/htmlcov/)
./scripts/dev/claude/style_check test --coverage
```

## Output schema (JSONL)

```json
{
  "file": "kernel/poll.c",
  "line": 42,
  "col": 7,
  "end_line": 42,
  "end_col": 15,
  "rule": "sizeof_parens",
  "severity": "error",
  "message": "sizeof requires parens: write sizeof(X), not sizeof X",
  "snippet": "int n = sizeof x;"
}
```

Exit code is `0` whatever the style findings, and `1` when a file
could not be parsed (see [Parsing](#parsing)) or an internal error
occurred.

## Architecture

Three layers:

1. **Structural (libclang).** `clang.cindex`, fed the exact compile
   commands of the builds (see [Parsing](#parsing)), to identify *what*
   each region of source is (function definition, block,
   if-statement). Used by rules that need to find specific
   constructs. Every read of a cursor's kind goes through
   `libclang.kind_of()`; a test forbids reading `.kind` directly.
2. **Token / spelling.** The un-preprocessed source text the user
   actually wrote. `STATIC void foo` and `static void foo` are
   visually identical and checked identically.
3. **Raw text.** Read the file as bytes. Used for whitespace, column
   counts, and a small state-machine comment scanner (libclang's AST
   doesn't include comments).

Python has the same three layers without a build: `pyctx.py` runs
`tokenize` and `ast` over the text, and gives the rules its tokens, its
tree, its multi-line brackets, its blocks (with the branches of each
if/elif/else and try/except chain) and its logical lines. A file Python
cannot parse is a `parse` problem, as for C, never skipped.

The tool **never** inspects macro definitions and **never** cares what
a macro expands to.

## Running the tests

The preferred entry point is the `test` sub-command (above). For
direct invocation:

```bash
PYTHONPATH=scripts/dev/claude python3 -m unittest \
   _style_check.tests.test_rules _style_check.tests.test_parsing \
   _style_check.tests.test_py_rules
```

The fixtures are kernel-style code that no build compiles. The tests
compile them, on paper, with the commands of the checkout's build
(`tests/fixture_build.py`): a temporary configuration listing every
fixture with the exact command of a kernel C file (a gtests C++ file
for the `.cpp` ones). So the tests need a build too, and the fixtures
take the path real sources take; `test_parsing.py` checks that every
fixture parses with no problem.

The rule test classes:

- `TestRulesOnFixtures` -- one test method per rule. Each runs the
  rule against `tests/fixtures/bad_<rule_id>.c` and asserts the
  expected diagnostic count.
- `TestRulesOnGoldenFiles` -- iterates every applicable rule across a
  curated 15-file kernel/userspace subset; any non-zero diagnostic
  count fails the build unless the (file, rule) pair is recorded in
  `KNOWN_DRIFT` with a justification, or the rule is in
  `GOLDEN_SKIP_RULES` for widespread corpus drift the user has
  flagged to clean up over time.

`test --coverage` runs the suite under `coverage.py` and writes an
HTML report to `scripts/dev/claude/htmlcov/index.html` (gitignored).

## Layout note

`scripts/dev/claude/style_check` is the executable bash shim;
`scripts/dev/claude/_style_check/` is the Python package (a single
filesystem name can't be both a file and a directory).
