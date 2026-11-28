# Tilck's Python style, as inferred from its author's code

Status: draft for review, 2026-11-28. It records what Vladislav's own Python
says about his style. style_check checks it (`style_check check` on any
Python file, see section 5); section 6 lists the open questions.

## 1. Where it comes from

The corpus is every Python file as of 77b85697 (2025-12-27), the last commit
before CLAUDE.md (2026-04-04). Every commit touching these files before that
date is the author's; two contributors added a line each. The corpus is 4,492
lines in 33 files:

- **The test runners**, the core of the corpus (2,206 lines): `run_all_tests`,
  `single_test_run`, `run_interactive_test` and `tests/runners/lib/*`.
- **The gdb scripts** (`other/gdb_scripts/*`, `other/tilck_unstripped-gdb.py`).
- **The interactive tests** (`tests/interactive/*.py`): snippets run by
  `exec()`.
- **The dev scripts** `generate_pci_ids`, `cmp_build_dirs`, `cmp_funcs_size`,
  and `count_lines_of_code`. The last one is from 2018-2019, with CamelCase
  names, and is older than the rest of the style.

Not in the corpus: everything written since, style_check and the
configurator included, and the vim test data
(`tests/interactive/vim_text_files/`).

The counts below come from a tokenize/ast pass over the corpus. "N/M" means
N of the M cases measured. Line numbers (`file:line`) are those of the
tree today, where the files have since moved on.

## 2. What the C style already says

The Python follows the same aesthetics as the C code, documented in
`docs/contributing.md` and recorded in style_check's preference notes. These
carry over unchanged:

- **80 columns, strictly.** No line of the corpus exceeds it.
- **The indentation width is the project's**: 3 spaces today, 4 after the
  switch. One block (run_all_tests:353) is indented by 2: a slip.
- **snake_case**, except in the 2018 `count_lines_of_code`.
- **Pretty over consistent.** Every rule can yield to a prettier result,
  except the 80-column limit. That is why most rules below are weights,
  not laws.
- **Harmony.** A line much longer than its neighbours is ugly, even under 80
  columns.
- **Column alignment** of related lines, as for `#define` values: enum
  members, constant blocks, typed declarations, backslashes.
- **Calls: Style 1 or Style 2, never the hybrid.** Arguments aligned under
  the first one after the bracket, or `(` ending the line with the arguments
  one level in and `)` on its own line.
- **Definitions: Style 1 only.** Parameters aligned under the first one,
  `):` closing the last line.
- **Compact form is per nesting level.** `Tuple[str,int]` and `ord('z')+1`
  appear inside otherwise spaced code.

## 3. What the Python shows

### 3.1 Vertical space: groups and the blank line after a header

This is the strongest pattern, and Python's form of contributing.md's "empty
line after `if (...) {`" rule.

**A body separated into groups by blank lines starts with a blank line after
its header. A body that is one group starts right away.**

| Header                 | Grouped body: blank after | Flat body: no blank |
|------------------------|--------------------------:|--------------------:|
| `def`                  |                   107/108 |               87/97 |
| `class`                |                     22/22 |               10/10 |
| `for` / `while`        |                     30/30 |               32/36 |
| `with`                 |                       3/3 |                 7/8 |
| `if` / `elif` / `else` |                     34/39 |             268/277 |
| `try` / `except`       |                       8/9 |               22/32 |

"Grouped" counts the blank lines between the body's direct statements, not
those inside nested blocks. An `if` whose only statement is a nested,
grouped `if` is flat:

```python
   if fail_on_zero:
      if sum(tests_to_run.values()) == 0:

         found = []
         ...
```

Where the rule bends:

- **A branch is spaced or compact as a whole**: the blank line after its
  header comes with a blank line before the next `elif` / `else` /
  `except` (69/73 pairs). And a short branch next to a grouped one is often
  spaced too, to match its peer (11/17):

  ```python
     if p.returncode == Fail.success.value:

        raw_print("[ PASSED  ] after {:.2f} seconds\n".format(elapsed))

     else:

        if not show_output:
           raw_print("")
           direct_print(bin_output)

        raw_print(
           ...
  ```

- **`try` is spaced more readily** (10 of 32 flat `try` bodies), mostly to
  match a spaced `except`.
- **A short header followed by a long first line** sometimes skips the blank
  line even with a grouped body. These are 5 of the 11 grouped bodies
  without one (`if val != exp:`, `if e.screen_text:`, `if h:`).
- **A body that is a single compound statement** (a `def` holding only a
  `try` or a `for`) is spaced about half the time. So is a body with a
  statement spanning several lines, or one starting with a comment: they
  look heavier than one group, and the blank line is the writer's choice.
- **A short method spaced like its neighbours**: in a class whose methods
  are spaced, a 2-line method is often spaced too.
- **A header spanning several lines is always followed by a blank line**
  (8/8), whatever its body: Python's form of C's `{` on its own line after
  a multi-line condition or signature.

### 3.2 Vertical space elsewhere

- **One blank line between top-level definitions** (146/156). Two occur 10
  times, mostly before `main()` or a big function; 3 once.
- **One blank line between methods.** A class's body is grouped when it has
  several methods, so `class X:` is followed by a blank line.
- **A blank line after an `if` that ends in `return`, `continue`, `break` or
  `raise`**, when more statements follow: 93/93. This is the C rule.
- **A `global` block is a declaration block**, followed by a blank line when
  the body is grouped (18/26). The 8 others are 3-line functions.
- **Two blank lines in a row inside a function** occur 4 times: slips.
- **Comments**: a comment line sits on the code it introduces. A blank line
  after a comment marks a section heading ("# Dumping gcda file").
  Paragraphs inside a long comment are separated by a bare `#` line.

### 3.3 Brackets and continuation lines

- **Hanging brackets** (`(`, `[` or `{` ending the line) indent their content
  one level (109/110). The closing bracket goes on its own line, at the
  statement's column (109/110). It is never glued to the last element.
- **Aligned brackets** (content after the opening bracket) align their
  continuation lines with the first element (42 cases), and close glued to
  the last element (26/26).
- **The last argument may open a hanging block**, closed by `])` at the
  statement's column (the interactive tests):

  ```python
  send_to_vm_and_find_text(r"dp{ret}", True, [
     "TilckDebugPanel",
     "TERM_ROWS",
  ])
  ```

- **Staircases**: a continuation line starting with `.` (a `.format(...)`
  or a method chain) is indented one level past the line it continues
  (10/14):

  ```python
        raw_print(
           "ERROR: No tests matching "
           "the '{}' regex having timeout <= {}"
              .format(reg, max_timeout)
        )
  ```

- **Backslash continuations are rare (9 lines)**, and their backslashes are
  aligned in one column, as in C macros:

  ```python
     rows = subprocess                             \
              .check_output(['nm', KERNEL_FILE])   \
                 .decode("utf-8")                  \
                    .split("\n")
  ```

- **Multi-line conditions** go in parentheses, break after `and` / `or`, and
  align under the parenthesis. The body starts after a blank line, Python's
  version of C's brace on its own line:

  ```python
        if (g_qemu_died and not g_keyboard_interrupt and
            get_fail_reason() != Fail.timeout):

           # It is unclear why ...
  ```

- **Trailing commas**: hanging lists have one (27/30). Hanging calls do not
  (68/71). Dicts mostly do not (7/9).

### 3.4 Spaces on a line

- **Keyword arguments**:
  - in definitions, spaces around `=` (15/15): `delay = 0.2`,
    `false_positive_handler = None`;
  - in calls with one argument per line, spaced (42/48): `action =
    "store_true"`. The 6 exceptions are three `subprocess.Popen` calls,
    written `stdin=subprocess.PIPE`;
  - in one-line calls, evenly split (8/16): `threading.Thread(target = run)`
    but `int(x, base=16)`.
- **Subscripts and calls are never padded inside**: `x[0]`, `f(a)` (310/310
  subscripts).
- **Comprehensions are padded** (9/13): `[ int(x, 16) for x in raw_arr ]`,
  `{ k: [] for k in TEST_TYPES }`. Plain literals are not (36/40):
  `['-enable-kvm', '-cpu', 'host']`. A literal of literals can be:
  `[ ['runall', ALL_TESTS_TIMEOUT] ]`.
- **Binary operators are spaced**, with compact forms inside call arguments
  (`ord('z')+1`, `tot-2`). The same per-level rule as C.
- **Dict colons come in three forms**: `k: v` (15), `k : v` (13), and values
  aligned in a column (32). Within one literal the form is meant to be one;
  the `TIMEOUTS` dict mixes them, a slip.
- **Annotation colons**: `x : int` (24) and `x: int` (21). The runners'
  newer code prefers `x : int`. The 2018 script and one line
  (`g_params:TestParamsType`) have the others.
- **Comments**: `# text` (310/312). An inline comment is one space after the
  code (42/52), or aligned with its neighbours.
- **`assert(...)` with parentheses** (6/6), as in C.

### 3.5 Other habits

- **`.format()`, not f-strings**: every runner disables pylint's
  `consider-using-f-string`.
- **Quotes have no rule**: `"..."` for messages and format strings (132/135
  format strings), either kind for short identifiers (365/276).
- **The file header**: shebang, then `# SPDX-License-Identifier:
  BSD-2-Clause`, then the `# pylint: disable=...` lines (with
  `bad-indentation` because of the 3-space width), a blank line, the imports.
- **Sections** are separated by `###...###` rule lines, before `main()` and
  before `if __name__ == '__main__':`.

## 4. Slips, not rules

The checker reports these, and does not learn from them: the 2-space
blocks (run_all_tests:353, mi_printer.py:62), the 4 double blank lines
inside functions, the mixed colons in `TIMEOUTS` (run_all_tests:50), and
the hanging bracket closed at the wrong column (regs_printer.py:32-33). The
CamelCase of `count_lines_of_code` is older than the style.

## 5. The checker's rules

The Python rules are style_check's (`rules/py_*.py`), on the same model as
its C rules: hard rules (defects), weighted preferences, and gradient costs
that lower a function's prettiness. `cols_80` and `trailing_ws` are shared
with C.

**Hard (-10):**

- `py_indent`: one `--indent` level per block, spaces only;
- `py_spdx_header`;
- `py_hanging_bracket`: a bracket ending its line indents its elements one
  level past the line it opens on, and closes on a line of its own, at
  that line's indentation;
- `py_aligned_bracket`: a bracket followed by its first element aligns the
  following elements under it, and closes after the last one;
- `py_blank_after_jump`: a blank line after an `if` ending in
  `return`/`continue`/`break`/`raise` when code follows.

**Strong (-3):**

- `py_blank_after_header`: a header spanning lines is followed by a blank
  line. Otherwise a grouped body starts after a blank line and a flat one
  does not, except that:
  - a flat body may be spaced when it is heavy (a single compound
    statement, a statement spanning lines, a leading comment), when a peer
    branch is spaced, in a `try` chain, or like an adjacent method;
  - a grouped body may skip the blank line when the header is shorter
    than the body's first line (contributing.md's rule for C).
- `py_branch_symmetry`: a spaced branch is followed by a blank line before
  the next `elif` / `else` / `except`, and a compact one is not.
- `py_def_default_spacing`: spaces around `=` in parameter defaults.

**Medium (-1.5) and soft (-0.5):**

- `py_blank_between_defs`: one blank line before a `def` or `class`
  (comments above it belong to it); two allowed at module level, pending
  question 2.
- `py_no_double_blank` inside functions and classes.
- `py_backslash_align`.
- `py_dict_colon_uniform`: one colon form per literal, or the values in a
  column.
- `py_assert_parens`: `assert(x)` when there is no message.

**Gradient:**

- `py_harmony`: the C model (per-function IQR outliers on line length,
  shared in `harmony.py`), with Python's natural long lines (message calls,
  string-heavy lines, lines ending in a comma) excused as in C. With C's
  calibration it reports nothing in the corpus, as its C twin reports 1
  line in the kernel's 24k.

**Not built yet**, waiting on question 1 or on more evidence:
`py_multiline_call_kw_spacing`, `py_comprehension_padding`,
`py_multiline_condition` and `py_staircase`. Quotes and annotation colons
stay out until question 3 is answered.

### Over the corpus

17 findings in 4,492 lines. 11 are the slips of section 4. The 6 others are
the judgment cases of question 5.

Over the rest of the tree's Python (written after CLAUDE.md), the rules
find what the conversion will fix: the configurator is 4-space indented
(clean at `--indent 4` but for 26 findings: blank lines after jumps and
multi-line headers, 4 lines over 80 columns); style_check's own code has
about 150, mostly blank-line and bracket alignment. The vim test data is
excluded (`tests/interactive/vim_text_files/.style.yml`).

## 6. Questions

1. **Keyword arguments in one-line calls**: `f(x, key = val)` and
   `f(x, key=val)` are evenly split. Does one of them win, or does it
   depend (on the call's length, on how many keywords it has)?
2. **Two blank lines between top-level definitions**: 10 times out of 156.
   Is a double blank line a section break you want, or a slip?
3. **Annotation colons**: `x : int` or `x: int`, or no preference?
4. **The blank line after a short header** whose body is grouped
   (`if val != exp:` followed by a long line): is skipping it the C
   "the body is longer than the header" exception, or a slip? The checker
   takes it as the exception today.
5. **Six findings in your code that are style or slip, your call:**
   - mobj_printer.py:19, `def children(self):` with a 2-group body and no
     blank line after the header;
   - cmp_funcs_size:83 and :93, `if before_funcs > after_funcs:` with a
     grouped body (`print()` lines, then a `for`) and no blank line;
   - count_lines_of_code:124, `if len(rsline) > LINE_MAX_COLS:`, the same;
   - detect_kvm.py:59 at 77b85697 (since rewritten), a compact `try:`
     before a spaced `except:`;
   - regs_printer.py:72, a spaced `if` branch with no blank line before
     its `else:`.
