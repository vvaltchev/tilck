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

### HARD (-10.0, error) -- 6 rules. True defects.

| Rule | Source |
|------|--------|
| `cols_80` | docs/contributing.md NOTE[1]: "the sole rule with NO exceptions" |
| `pragma_once` | Required header convention; build relies on it |
| `spdx_header` | License/policy requirement (Tilck-authored files) |
| `indent_3sp` | Project convention; tabs are never accepted |
| `include_order` | Q13 hard: gen_headers must be first or build breaks; subtree grouping required |
| `per_case_braces_when_locals` | Q12: C language semantics -- case labels do not introduce scope |

### SOFT-STRONG (-3.0, warning) -- 19 rules. Things the user explicitly labeled "hard rule" / "forbidden" / "must" in CLAUDE.md or preferences but whose violation is cosmetic (compiles + runs the same).

| Rule | Source |
|------|--------|
| `while_true_only` | Q31: "V1 only allowed form" |
| `pointer_asterisk_attached` | Q21: both wrong forms forbidden |
| `function_def_no_style2` | Q4: Style 2 = hard-no for function definitions |
| `switch_case_indent` | Q12: case-flush-with-switch forbidden |
| `break_before_operator_forbidden` | Q25: operator-at-start-of-line forbidden |
| `multiline_call_style` | Q1: Style 3 hard-no |
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
| `no_trailing_enum_comma` | Cosmetic |
| `trailing_ws` | Hygiene, doesn't break anything |

## Total

31 rules: 6 HARD + 19 SOFT-STRONG + 3 SOFT-MEDIUM + 3 SOFT-MILD.

The accumulated prettiness for a sloppy file can easily reach
-50 across multiple soft rules even with zero hard violations.
A file's prettiness score is informative whether or not it
"fails" -- the soft-only profile gives a continuous-scale read
on style debt.

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

- **Per-statement aggregation** -- multiple soft diagnostics on
  the same statement should normalize: a single ugly statement
  costs at most ~3-5 prettiness, not the unbounded sum.

- **Cascade scoring** (Q1b H1/H2/H3/H4) -- call-site formatting
  has a hierarchy where the highest-fitting form is the one to
  pick. The score for "Style 1 multi-arg" depends on whether H1
  (single-line) was feasible.

## Process rules -- NOT enforced by the tool

Per `docs/preferences-notes.md` (Q19, Q28):

- Naming-content choices (which subsystem prefix, which verb)
- API-design decisions (parameter bundling, function decomposition)
- Comma-expression macros (Claude shouldn't generate; existing
  ones not rewritten)
- DEBUG_ONLY vs `if (DEBUG_CHECKS)` choice
- Helper-file placement

These belong in Claude's behavior / CLAUDE.md, not in the linter.
