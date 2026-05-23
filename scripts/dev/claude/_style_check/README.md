# style_check -- Tilck C coding-style checker

A libclang-based linter that reports formatting violations against the
user's C coding style as machine-readable JSONL. Built for Claude to
consume so it can apply the style correctly without having to memorize
every nuance.

## Rules (all C-only)

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
  `no_trailing_enum_comma`, `one_stmt_per_line`,
  `fn_body_brace_own_line`, `multiline_call_style`,
  `pointer_asterisk_attached`, `switch_case_indent`,
  `blank_line_after_decl_block`, `non_const_locals_top_of_block`,
  `function_def_no_style2`, `no_packed_case_labels`,
  `per_case_braces_when_locals`, `empty_body_braces`,
  `no_packed_enum_values`).
- **Comments** -- `comment_block_multiline_format`
  (state-machine scanner + raw text).

## Installation

```bash
# Linux
sudo apt install python3-clang libclang-cpp14

# macOS
pip install clang

# FreeBSD
pkg install py311-clang
```

Run `./scripts/cmake_run && make` in the repo first to populate
`build/compile_db/compile_commands.json`.

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

Exit code is always `0` (report-only) unless an internal error
occurred (then `1`).

## Architecture

Three layers:

1. **Structural (libclang).** `clang.cindex` +
   `compile_commands.json` to identify *what* each region of source is
   (function definition, block, if-statement). Used by rules that
   need to find specific constructs.
2. **Token / spelling.** The un-preprocessed source text the user
   actually wrote. `STATIC void foo` and `static void foo` are
   visually identical and checked identically.
3. **Raw text.** Read the file as bytes. Used for whitespace, column
   counts, and a small state-machine comment scanner (libclang's AST
   doesn't include comments).

The tool **never** inspects macro definitions and **never** cares what
a macro expands to.

## Running the tests

The preferred entry point is the `test` sub-command (above). For
direct invocation:

```bash
PYTHONPATH=scripts/dev/claude python3 -m unittest _style_check.tests.test_rules
```

Two test classes:

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
