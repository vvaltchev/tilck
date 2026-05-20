# style_check -- Tilck C coding-style checker

A libclang-based linter that reports formatting violations against the
user's C coding style as machine-readable JSONL. Built for Claude to
consume so it can apply the style correctly without having to memorize
every nuance.

## What it does (v1)

Checks C source (`.c` and `.h`) for 5 rules:

| ID | Description |
|----|-------------|
| `cols_80` | Strict 80-column limit (the only rule with no exceptions) |
| `pragma_once` | Headers must use `#pragma once`, not `#ifndef _X_H_` guards |
| `sizeof_parens` | `sizeof(X)` always, never `sizeof X` |
| `static_fn_def_type_own_line` | When a static fn signature wraps, return type goes on its own line |
| `comment_block_multiline_format` | Multi-line `/* ... */` comments use ` * ` prefix on interior lines |

C++ (`.cpp` / `.hpp`) is out of scope for v1. v2 will add another 36
rules and revisit C++.

## Installation

The tool depends on libclang and its Python bindings.

```bash
# Linux
sudo apt install python3-clang libclang-cpp14
# (or whichever libclang version matches your system clang)

# macOS
pip install clang

# FreeBSD
pkg install py311-clang
```

For full functionality the tool reads
`build/compile_db/compile_commands.json`. Run `./scripts/cmake_run && make`
in the repo before checking files for the first time.

## Usage

```bash
# Check one or more files
./scripts/dev/claude/style_check check kernel/poll.c kernel/exit.c

# Check all files changed since a git ref
./scripts/dev/claude/style_check check --since HEAD~5

# Text output (default is JSONL for Claude consumption)
./scripts/dev/claude/style_check check --format text kernel/poll.c

# Restrict to one rule
./scripts/dev/claude/style_check check --rule cols_80 kernel/poll.c

# Skip one rule
./scripts/dev/claude/style_check check --exclude-rule sizeof_parens kernel/poll.c

# List all rules
./scripts/dev/claude/style_check list-rules

# Explain a rule
./scripts/dev/claude/style_check explain static_fn_def_type_own_line
```

## Output format

JSONL (one diagnostic per line). Each diagnostic:

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

Exit code: always `0` (report-only) unless an internal error occurred,
in which case `1`.

## Architecture

Three layers:

1. **Structural (libclang).** `clang.cindex` + `compile_commands.json`
   to identify *what* each region of source is. Used by rules that
   need to find function definitions, blocks, etc.
2. **Token / spelling.** Raw token stream from libclang -- gives us
   the un-preprocessed text the user wrote.
3. **Raw text.** Read the file as bytes. Used for whitespace, column
   counts, and comment scanning (a small state machine because
   libclang's token API can miss comments inside macros or after line
   continuations).

The tool **never** inspects macro definitions and **never** cares what
a macro expands to. `STATIC void foo` and `static void foo` are
visually identical and checked identically.

## Running the tests

```bash
cd $REPO_ROOT
PYTHONPATH=scripts/dev/claude python3 -m _style_check.tests.test_rules
```

## What's NOT implemented (v2 work)

- The other 36 hard rules from the plan.
- Cross-function uniformity (Style 1/2/3 cluster enforcement).
- Subjective ranking (prefer local-extraction over brace-on-own-line).
- C++ rules (will be added once `tests/unit/` is polished).
- Pre-commit / CI integration.
- Autofix (Claude does the fixes by reading the diagnostics).

See `/home/vlad/.claude/plans/as-you-see-can-vast-bonbon.md` for the
complete plan.
