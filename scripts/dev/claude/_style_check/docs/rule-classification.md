# Rule classification audit

Catalog of every rule in `_style_check/rules/`, with the chosen
severity tier, default per-diagnostic score, and the source that
justifies the classification. Score tiers are defined in
`rules/base.py`.

The user's framework: **a few hard rules, a ton of soft rules with
floating-point scores.** A "hard rule" is a defect that breaks
build/semantic/policy. Style preferences -- even those the user
called "hard rule" in casual language during the Q&A loop -- are
typically SOFT-STRONG: still strongly enforced, but the violation
is a prettiness loss, not a defect. The checker should not "fail"
on cosmetics.

## Score tiers (signed doubles, defined in `base.py`)

| Constant            | Value | Severity | Meaning |
|---------------------|-------|----------|---------|
| `SCORE_HARD_RULE`   | -10.0 | error    | Defect (build, policy, C semantics) |
| `SCORE_STRONG_PREF` |  -3.0 | warning  | Explicit "hard rule" in user prefs but cosmetic in nature |
| `SCORE_MEDIUM_PREF` |  -1.5 | warning  | Convention, "ugly", "avoid" |
| `SCORE_SOFT`        |  -0.5 | warning  | Mild preference (e.g. hex case) |
| `SCORE_NUDGE`       |  -0.2 | warning  | Low-threshold preference |
| `SCORE_CONTEXT_OK`  |  +0.5 | --       | Bonus when a soft rule is legitimately overridden |

## Classification

### HARD (-10.0, error) -- 7 rules. True defects and load-bearing visual rules.

| Rule | Source |
|------|--------|
| `cols_80` | docs/contributing.md NOTE[1]: "the sole rule with NO exceptions" |
| `pragma_once` | Required header convention; build relies on it |
| `spdx_header` | License/policy requirement (Tilck-authored files) |
| `indent_3sp` | Project convention; tabs are never accepted |
| `include_order` | Q13 hard: gen_headers must be first or build breaks; subtree grouping required |
| `per_case_braces_when_locals` | Q12: C language semantics -- case labels do not introduce scope |
| `multiline_call_style` | User-promoted from SOFT-STRONG: Style 3 must be an error, not a warning, because it's easy to overlook in review (user note 2026-05-27) |

### SOFT-STRONG (-3.0, warning) -- 18 rules. Things the user explicitly labeled "hard rule" / "forbidden" / "must" in CLAUDE.md or preferences but whose violation is cosmetic (compiles + runs the same).

| Rule | Source |
|------|--------|
| `while_true_only` | Q31: "V1 only allowed form" |
| `pointer_asterisk_attached` | Q21: both wrong forms forbidden |
| `function_def_no_style2` | Q4: Style 2 = hard-no for function definitions |
| `switch_case_indent` | Q12: case-flush-with-switch forbidden |
| `break_before_operator_forbidden` | Q25: operator-at-start-of-line forbidden |
| `cast_no_asymmetric_form` | Q22: `(Type*) expr` forbidden |
| `empty_body_braces` | Q44: bare `;` body forbidden |
| `no_packed_case_labels` | Q42: V2 packed = hard rule violation |
| `no_packed_enum_values` | Q23: V3 packed enum forbidden |
| `non_const_locals_top_of_block` | Q15: top-of-scope hard rule |
| `blank_line_after_decl_block` | Q18: hard rule 1 |
| `blank_line_after_non_final_return` | Q18: hard rule 2 |
| `void_arglist` | CLAUDE.md: empty arg list spelled `(void)` |
| `fn_body_brace_own_line` | CLAUDE.md: function body `{` on own line |
| `static_fn_def_type_own_line` | CLAUDE.md: "uniformly, even for short signatures" |
| `one_stmt_per_line` | CLAUDE.md: don't pack statements |
| `else_same_line_as_brace` | CLAUDE.md |
| `sizeof_parens` | CLAUDE.md: "always" |

### SOFT-MEDIUM (-1.5, warning) -- 3 rules. Conventions / cosmetic ugliness.

| Rule | Source |
|------|--------|
| `comment_block_multiline_format` | CLAUDE.md: multi-line `/* */` interior lines start with ` * ` |
| `endif_annotation_long_blocks` | Q38: mandatory at 100+ lines but threshold is fuzzy |
| `no_void_cast_discard` | Kernel doesn't use; stylistic |

### SOFT-MILD (-0.5, warning) -- 3 rules. Small accumulating preferences.

| Rule | Source |
|------|--------|
| `hex_literal_lowercase` | User-confirmed soft preference |
| `enum_trailing_comma` | Cosmetic |
| `trailing_ws` | Hygiene, doesn't break anything |

## Total

31 rules: 7 HARD + 18 SOFT-STRONG + 3 SOFT-MEDIUM + 3 SOFT-MILD.

The accumulated prettiness for a sloppy file can easily reach
-50 across multiple soft rules even with zero hard violations.
A file's prettiness score is informative whether or not it
"fails" -- the soft-only profile gives a continuous-scale read
on style debt.

## Gradient growth profiles

Gradient (NUDGE-tier) rules pick one of three shapes for how their
cost grows with occurrence count in a locality:

1. **Flat per-occurrence** -- a fixed `prettiness_cost` per
   occurrence, no compounding. Used for rules where each
   individual occurrence is mildly ugly and stacking is just
   "more of the same" (`prefer_nullptr`, `prefer_cpp_cast`,
   `cast_density`, `ifdef_density`, `paren_explicit_precedence`,
   `typed_literal_suffix`, `else_after_return`,
   `no_void_cast_discard`, etc.).

2. **Super-linear in a locality** -- the rule scans a locality
   (typically a statement), counts occurrences, and emits a
   single diagnostic whose `prettiness_cost` follows a
   super-linear formula (e.g. quadratic `base * (N-1)^2`). The
   first occurrence may be free if it's legitimate in isolation.
   Beyond a threshold, the cost crosses
   `STATEMENT_HARD_FAIL_THRESHOLD = 3.0` and the diagnostic is
   surfaced as a HARD-FAIL in the reporter (red, factors into
   the function's `broken` verdict). Used so far for
   `qualified_name_density` (`::` chains); the same shape applies
   naturally to other "compounding ugliness" rules.

3. **State-dependent / catastrophic** -- the rule first detects
   whether a "baseline pattern" exists (e.g. some lines in a
   cluster are aligned); if yes, every deviation is much uglier
   than its isolated cost would suggest. Reserved for alignment
   rules; `call_cluster_column_align` and `align_multiline_operators`
   are candidates.

The aggregator (`_compute_prettiness` in `aggregator.py`) **no
longer clamps** line scores or function prettiness to [0, 1]; a
function with a HARD-FAIL gradient and many soft hits can score
deep negative, which is the intent.

`Diagnostic.is_hard_failure` returns True for:
- a non-gradient diagnostic with `severity == 'error'`, OR
- a gradient diagnostic with
  `prettiness_cost >= STATEMENT_HARD_FAIL_THRESHOLD`.

`FunctionRegion.hard_failure_gradients` counts the latter, and
the verdict path treats them the same as hard violations.

## Context-sensitive rules to add

The static per-rule classification above is the floor. The
preferences corpus is loaded with context-dependent rulings that
deserve their own scoring logic:

- **`harmony_with_neighbors`** (Q22b clarification) -- a line under
  80 cols may still be "ugly" if it stands out from the local
  neighborhood. Compute a windowed mean/std-dev of line lengths
  in a basic block; flag outliers as SOFT.

- **`align_multiline_operators`** (Q10) -- `||` / `&&` operators
  in a multi-line condition should be column-aligned. Detect and
  penalize misalignment as SOFT-MEDIUM.

- **`operator_past_close_paren`** (Q25 refinement) -- operators
  in a wrapped condition should sit in a column past the closing
  `)` on the last line. SOFT.

- **Per-function aggregation** -- group diagnostics by enclosing
  function (libclang gives function extents), report
  `total_prettiness` and `normalized_score` (total / statements).

- **Per-statement aggregation** -- the OPPOSITE direction from the
  earlier "normalize to a cap" plan: when ugliness concentrates in
  one statement, accumulation should *accelerate*, not cap. Tightly
  clustered violations are psychologically far uglier than the same
  count spread across a function. Rules that match this profile
  (qualified_name_density, alignment rules, etc.) emit super-linear
  cumulative cost; rules whose individual occurrences don't
  compound (prefer_nullptr, ifdef_density) keep flat per-occurrence
  cost. The aggregator no longer clamps line or function prettiness
  to [0, 1]; catastrophic statements must read as catastrophic.

- **Cascade scoring** (Q1b H1/H2/H3/H4) -- call-site formatting
  has a hierarchy where the highest-fitting form is the one to
  pick. The score for "Style 1 multi-arg" depends on whether H1
  (single-line) was feasible.

## Special header conventions

Two categories of headers don't follow the standard "must have
`#pragma once`" rule. The tool recognises them automatically or
via an explicit marker comment.

### `.c.h` implementation-include files

Tilck convention: a file with extension `.c.h` (e.g.
`kernel/kmalloc/general_kmalloc.c.h`,
`include/tilck/common/elf_get_section.c.h`) is an
implementation-include included by exactly one .c file. It's
not a standalone header. The tool detects these by extension
and exempts them from `pragma_once` automatically -- no user
action needed.

### Multi-include / X-macro headers

Some headers are intentionally re-includable: each include site
sets up different macros before pulling them in, expanding the
header into different code each time. Canonical Tilck examples:

  - `include/tilck/common/cmdline_opts.h` (DEFINE_KOPT table)
  - `tests/system/cmds_table.h` (CMD_ENTRY table)

These MUST NOT have `#pragma once` and are typically included
indented inside a `#define ... #undef` block. Tag the header
with a marker comment near the top:

```c
/* SPDX-License-Identifier: BSD-2-Clause */
/* style_check: multi-include */

/* <rest of the file comment> */
```

Either of these spellings is recognised:

  - `/* style_check: multi-include */`
  - `/* style_check: re-includable */`

The tool exempts marked headers from `pragma_once`.

### Indented `#include` directives

`include_order` only checks `#include` lines that start at
column 1. The user's existing convention -- indenting an
X-macro `#include` inside a `#define ... #undef` block -- is
the signal that the directive is in a special context, not
part of the file's top-of-file include block:

```c
#define DEFINE_KOPT(name, alias, type, default) type kopt_##name = default;
   #include <tilck/common/cmdline_opts.h>
#undef DEFINE_KOPT
```

The indented `#include` is skipped by the rule.

## Per-directory configuration: `.style.yml`

Drop a `.style.yml` in any directory to apply settings to that
directory and every subdirectory. Settings cascade: every config
from the repo root down to the file's parent directory is
applied in order, deeper configs merging with / overriding
shallower ones.

### Schema

```yaml
# .style.yml

# Skip the file entirely -- no rules run, no diagnostics produced.
# In `--all` output the file is shown as `[SKIP]` (dim cyan).
ignore: true

# Disable specific rules. The set UNIONS with parent configs:
# if the root says `disabled: [cols_80]` and this directory adds
# `disabled: [pragma_once]`, both rules are disabled here.
disabled:
  - cols_80
  - pragma_once

# Whitelist: only run these rules. REPLACES any parent setting
# (last `enabled_only` on the path wins).
enabled_only:
  - sizeof_parens
  - hex_literal_lowercase
```

All three keys are optional and may be combined. With both
`enabled_only` and `disabled` set, `enabled_only` is applied
first (narrow to the whitelist) and `disabled` removes anything
unwanted from that whitelist.

### Canonical usage in Tilck

Vendored / system header directories carry `ignore: true`:

  - `include/system_headers/.style.yml`
  - `include/3rd_party/.style.yml`
  - `common/3rd_party/.style.yml`

The files in these directories preserve upstream style; nothing
is diffed against the Tilck conventions.

### Cascading example

```
repo/.style.yml:
  disabled:
    - rule_a

repo/lib/.style.yml:
  disabled:
    - rule_b
  enabled_only:
    - rule_b
    - rule_c
```

For `repo/lib/foo.c`:

  - `disabled = {rule_a, rule_b}` (unioned)
  - `enabled_only = {rule_b, rule_c}` (replaced by deeper config)
  - Effective rule set: `{rule_c}` (only rule_b/rule_c
    whitelisted; rule_b also disabled; rule_a not in whitelist).

## Process rules -- NOT enforced by the tool

Per `docs/preferences-notes.md` (Q19, Q28):

- Naming-content choices (which subsystem prefix, which verb)
- API-design decisions (parameter bundling, function decomposition)
- Comma-expression macros (Claude shouldn't generate; existing
  ones not rewritten)
- DEBUG_ONLY vs `if (DEBUG_CHECKS)` choice
- Helper-file placement

These belong in Claude's behavior / CLAUDE.md, not in the linter.
