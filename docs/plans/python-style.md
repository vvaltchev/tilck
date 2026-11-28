# Tilck's Python style, as inferred from its author's code

Status: reviewed with the author, 2026-11-28. It records what Vladislav's
own Python says about his style, and his answers where the code alone could
not say (section 6). style_check checks it (`style_check check` on any
Python file, see section 5).

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

**Behind all of it, vertical space is weight** (the author's words, from
the review):

- **The more structure a neighbour has inside, the more space may separate
  it from the next thing**: one level above the blank lines inside it. This
  holds at every level: between the groups of a function, between methods,
  between classes (3.2).
- **A blank line needs a reason too.** Two related one-line statements that
  read well together stay together:

  ```python
  new_x = compute_x(x, t)
  new_y = compute_y(y, t)
  ```

  176 of the 176 pairs of parallel one-line statements in the corpus have
  no blank line between them.
- **A light block cannot carry extra air.** A block of a few short, sparse
  lines looks worse with a blank line after its header or before the next
  branch, even when it has groups inside (count_lines_of_code:124, where
  even the blank line at 127 is optional).
- **Spacing tells which path is the common one.** Symmetric spacing says
  "two equally likely alternatives", which is rare (regs_printer.py:64-77,
  below).
- **The emphasis is the writer's call**: a blank line may set the key
  statement of a function apart (get_cmds.py:29, `task =
  tasks.get_task(tid)`), and a group of prints may read as the title of
  what follows (cmp_funcs_size:83).

No script can weigh these: style_check's blank-line rules are soft
warnings, which Claude judges in context, reading the code.

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
  `except` (69/73 pairs), when the branches weigh about the same. The
  other 4 are not slips: the common path is light, and the rare one is
  dense and spaced (regs_printer.py, where the `else` handles one specific
  function):

  ```python
        if resume_eip_str.find("asm_save_regs_and_schedule") == -1:

           # Actual value of the stack segment
           ss = r["ss"] & 0xffff

           res += [
              ("ss          ", fixhex16(ss)),
           ]
        else:

           # asm_save_regs_and_schedule() does NOT save SS on the stack.
           # What we see there instead is its 1st argument, caller's ...
           calc_eip = gdb.parse_and_eval("(void *){}".format(r["ss"]))
           res.append(("[true_eip]  ", calc_eip))
  ```

  And a short branch next to a grouped one is often spaced too, to match
  its peer (11/17):

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
  match a spaced `except`. A grouped `try` body may also skip the blank
  line: `try:` is too short to hide anything (detect_kvm.py at 77b85697).
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
  `raise`**, when more statements follow: 93/93. This is the C rule. Soft
  like every blank-line rule, but with a higher penalty.
- **A `global` block is a declaration block**, followed by a blank line when
  the body is grouped (18/26). The 8 others are 3-line functions.
- **Two blank lines (or three) are a section break, one level above the
  single ones.** All 20 runs in the corpus have a neighbour with groups
  inside:
  - inside a function, they close a run of groups: the argument parsing
    before the main logic (list_cmds.py:35), the checks before the final
    `sleep` and `continue` (single_test_run:340);
  - in a class, they set a big method with groups inside apart from a
    small one (mobj_printer.py:92, before a one-line `to_string()`);
  - at module level, they separate classes (2 or 3, whichever looks
    better: mi_printer.py:40) or `main()` from the helpers
    (generate_pci_ids:209).

  With no groups around, two blank lines are a slip. They are never
  required: 9 definitions with a double blank line inside are followed by
  a single one.
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

- **Keyword arguments and defaults follow PEP 8**, by the author's
  decision: `key=value` in calls, `param=default` in definitions, and
  `param: T = default` once annotated. The corpus had spaces around `=`
  (definitions 15/15, calls with one argument per line 42/48, one-line
  calls 8/16), and was converted (2158c285).
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
- **Annotations are `name: T`**, by the author's decision. The corpus had
  `x : int` (24, run_all_tests) and `x: int` (21, the other files), and was
  converted. A column of annotated assignments keeps its types aligned,
  padding after the colons:

  ```python
  tests_by_type: Dict[str, List[Tuple[str,int]]] = { k: [] for k in TEST_TYPES }
  tests_to_run:  Dict[str, int]                  = { k:  0 for k in TEST_TYPES }
  ```
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

The checker reports these, and does not learn from them; the author
confirmed each, and they are fixed (ae7acacb): the 2-space blocks
(run_all_tests:353, mi_printer.py:62), the mixed colons in `TIMEOUTS`
(run_all_tests:50), the hanging bracket closed at the wrong column
(regs_printer.py:31-33), and the missing blank line after `def
children(self):` (mobj_printer.py:19, minor). The CamelCase of
`count_lines_of_code` is older than the style.

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
- `py_call_kw_spacing`: `key=value` in calls;
- `py_def_default_spacing`: `param=default`, `param: T = default`;
- `py_annotation_spacing`: `name: T`, a column of them kept aligned.

**Vertical space: soft warnings, judged in context (3.1).** Each finding
says so in its suggestion, and none has a fix:

- `py_blank_after_jump` (-3, the heaviest): a blank line after an `if`
  ending in `return`/`continue`/`break`/`raise` when code follows;
- `py_blank_after_header` (-1.5): a header spanning lines is followed by a
  blank line. Otherwise a grouped body starts after a blank line and a
  flat one does not, except that:
  - a flat body may be spaced when it is heavy (a single compound
    statement, a statement spanning lines, a leading comment), when a peer
    branch is spaced, in a `try` chain, or like an adjacent method;
  - a grouped body may skip the blank line when it is small (4 code lines
    or fewer: 36 of the corpus's 40 are spaced, and the other 4 are
    correct too), or when the header is shorter than the body's first line
    (contributing.md's rule for C);
- `py_blank_line_runs` (-1.5): two or three blank lines need a section
  with groups inside on one side, at any level; four or more never;
- `py_blank_between_defs` (-1.5): a blank line before a `def` or `class`
  (comments above it belong to it);
- `py_branch_symmetry` (-0.2, a hint): a spaced branch followed by no
  blank line before the next `elif` / `else` / `except`, or the reverse.

**Medium (-1.5) and soft (-0.5):**

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

**Not built yet**, waiting on more evidence: `py_comprehension_padding`,
`py_multiline_condition` and `py_staircase`. Quotes stay out.

### Over the corpus

17 findings in 4,492 lines, before the review. 6 are the slips of section
4, fixed since (ae7acacb). The author found the other 11 correct: 5 double
blank lines and a triple one (3.2), and 5 blank lines after headers or
before branches that a script cannot weigh (section 6, answers 4 and 5).

With the rules as the review left them, the author's code has no hard
finding, and 3 soft ones, all judged correct: cmp_funcs_size:83 and :93
(the title prints) and regs_printer.py:72 (the light common path).

Over the rest of the tree's Python (written after CLAUDE.md), the rules
find what the conversion will fix: the configurator is 4-space indented
(clean at `--indent 4` but for 26 findings: blank lines after jumps and
multi-line headers, 4 lines over 80 columns); style_check's own code has
about 150, mostly blank-line and bracket alignment. The vim test data is
excluded (`tests/interactive/vim_text_files/.style.yml`).

## 6. Questions, answered

1. **Keyword arguments**: `key=value`, PEP 8's (3.4).
2. **Two blank lines between top-level definitions**: a section break, as
   two blank lines are at every level when a neighbour has groups inside
   (3.2).
3. **Annotation colons**: `name: T` everywhere (3.4).
4. **A short header over a grouped body, with no blank line**
   (utils.py:84, `if val != exp:` over a long line): correct, the C rule's
   exception.
5. **The findings the author judged** (3.1):
   - mobj_printer.py:19, `def children(self):` with a 2-group body and no
     blank line: a slip, minor;
   - cmp_funcs_size:83 and :93: correct, the three prints are the title of
     the loop that follows;
   - count_lines_of_code:124: correct, the block is too small for a blank
     line;
   - detect_kvm.py:46 at 77b85697: correct, `try:` is too short to need
     one;
   - regs_printer.py:72: correct, the light common path flows into the
     `else` of the rare one.
6. **An assignment and the `if` that tests it**: no rule. The blank line
   between them is optional, and may set the key statement apart
   (get_cmds.py:29).
