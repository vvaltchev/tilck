# style_check -- Tilck C coding-style checker

A libclang-based linter that reports formatting violations against the
user's C coding style as machine-readable JSONL. Built for Claude to
consume so it can apply the style correctly without having to memorize
every nuance.

## Rules (16, all C-only)

C++ (`.cpp` / `.hpp`) is intentionally out of scope. The `tests/unit/`
gtests are not yet curated and don't reflect the user's full C++
preferences.

### File-level / raw text

| ID | What it checks |
|----|---------------|
| `cols_80` | Strict 80-column limit (the one rule with no exceptions per `docs/contributing.md` NOTE[1]) |
| `indent_3sp` | Tab characters in leading whitespace (must use 3 spaces) |
| `trailing_ws` | Trailing space/tab at end of line |
| `spdx_header` | `/* SPDX-License-Identifier: BSD-2-Clause */` on line 1 of Tilck-authored files |
| `pragma_once` | `.h` files use `#pragma once`, not `#ifndef _X_H_` header guards |

### Tokens (raw spelling, with comment / string masking)

| ID | What it checks |
|----|---------------|
| `sizeof_parens` | `sizeof(X)` always, never `sizeof X` |
| `hex_literal_lowercase` | `0xab` not `0xAB` or `0X1234` |
| `no_void_cast_discard` | No `(void)expr` casts; kernel doesn't use this pattern |
| `one_stmt_per_line` | Don't pack `close(a); close(b);` on one line |
| `else_same_line_as_brace` | `} else {` and `} else if (...)` on the same line as `}` |

### Comments (state-machine scanner + raw text)

| ID | What it checks |
|----|---------------|
| `comment_block_multiline_format` | Multi-line `/* ... */` interior lines start with ` * ` |

### Structural (libclang + raw text)

| ID | What it checks |
|----|---------------|
| `static_fn_def_type_own_line` | When a static fn signature *wraps* across lines, the return type must be on its own line |
| `void_arglist` | Empty arg list is `(void)`, not `()` |
| `no_trailing_enum_comma` | No comma after the last enumerator |
| `fn_body_brace_own_line` | Function body `{` on its own line |
| `multiline_call_style` | No Style 3 ("top-heavy hybrid" -- args on one indented line, `);` at end) |

## Deferred to v2

The following rules from the original plan were dropped or deferred
during M2 because the corpus (the ground truth) contradicted the
documented "hard rule" interpretation, or because clean detection
needs scope-tracking the tool doesn't yet do. Each is marked with the
reason.

**Corpus contradicts the hard-rule reading:**
- `null_check_no_null` -- corpus uses `!= NULL` extensively in ASSERTs
  / `LIKELY()` / plain ifs (kernel/poll.c:76, fork.c:71,
  execve.c:407). `docs/contributing.md` says "generally", not
  "always".
- `non_const_locals_top_of_block` -- corpus regularly uses C99
  mixed-declaration style (kernel/signal.c:21-33, kernel/poll.c:21).
  Other files do use C89 top-of-block style (kernel/fork.c:62-67).
  Both forms accepted in practice.

**Needs scope-depth tracking:**
- `goto_label_flush_left` -- function-scope labels (kernel/fork.c:
  175-177) are flush-left, but labels nested in block scopes
  (userapps/devshell/devshell.c:286) are indented to match. Distinguishing
  the two needs scope analysis.

**Needs structural reasoning v1 doesn't support well:**
- `pointer_asterisk_attached` -- needs to distinguish pointer decl
  from multiplication, function-pointer types, casts.
- `cast_space_after_close_paren` -- needs to identify `(type)`
  casts vs function calls vs control-flow conditions.
- `no_trailing_arg_comma` -- needs argument-list scoping in calls
  AND declarations.
- `wrapped_if_brace_own_line` -- needs to detect multi-line
  conditions.
- `single_stmt_no_braces` -- needs to count body statements.
- `nested_if_brace_propagation` -- needs to walk if-chain shape.
- `control_brace_same_line` -- needs to handle the multi-line
  condition case correctly.
- `blank_line_after_brace` -- needs header-vs-body length comparison.
- `empty_body_braces` (`while (1);` vs `while (1) { }`) -- needs to
  skip `do { ... } while (cond);`.
- `long_fn_sig_args_aligned` -- multi-line function signatures.
- `fn_args_named_in_decls` -- function declarations vs definitions
  distinction.
- `unused_param_name` -- needs use analysis on PARM_DECL.
- `inline_fn_type_same_line` -- needs to identify `static inline`
  short defs vs regular static defs.
- `designated_init` -- needs to identify struct vs array initializers.
- `struct_field_inline_comment_align` -- needs per-struct alignment
  analysis.

**Macro-block alignment (needs block-scoping heuristic):**
- `define_value_align`
- `define_backslash_col`
- `define_value_inline_comment_align`
- `nested_ifdef_indent`
- `ifdef_indent_small` / `ifdef_no_indent_large`

**Comment-style heuristic (soft rule):**
- `macro_do_while`
- `comment_line_in_c_files` (`//` permitted only for brief
  one-liners -- "brief" is subjective).

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

# List all rules
./scripts/dev/claude/style_check list-rules

# Explain a rule
./scripts/dev/claude/style_check explain multiline_call_style
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

```bash
PYTHONPATH=scripts/dev/claude python3 -m unittest _style_check.tests.test_rules
```

All 17 tests should pass. The "golden files" check (kernel/poll.c,
execve.c, sched.c, fork.c, signal.c, kmutex.c, headers, userapps)
should report zero diagnostics from any registered rule.

## Two known legacy-drift findings in canonical files

These were caught by `cols_80` during M1 verification:
- `kernel/exit.c:244` -- 107 cols
- `kernel/elf.c:446` -- 83 cols

Both are real over-limit lines in older code, not tool bugs. They're
not in the golden test set (we use a 15-file clean subset); fix at the
user's discretion.

## Layout note

`scripts/dev/claude/style_check` is the executable bash shim;
`scripts/dev/claude/_style_check/` is the Python package (a single
filesystem name can't be both a file and a directory).
