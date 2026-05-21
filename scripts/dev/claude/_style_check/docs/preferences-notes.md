# Style-checker preference notes

Free-form observations from the v2 ranked-preference loop that don't
reduce to a single per-question entry in `preferences.yaml`. Organized
by topic; append-only.

The `preferences.yaml` file is for individual ranked-snippet questions
and their answers. This file is for things that emerge across multiple
questions, principles that apply transversally, or pieces of user
feedback that explain *why* a ranking went the way it did but are too
broad to attach to one question.

---

## Decision trees (cascading preferences)

### Call-site layout: hierarchy of wrapping forms

Source: Q1 + Q1b clarification, 2026-05-20.

For an assignment of the form `lhs = func(args);` that's too long for
80 cols, the user picks the highest-fitting form from this strictly-
ordered cascade. Each step is a fallback for the previous step not
fitting.

  H1. Single line:
        `lhs = func(args);`
      Always tried first. If it fits in 80 cols, stop.

  H2. Break at `=`:
        lhs =
           func(args);
      Use when H1 doesn't fit but the call alone (`func(args);`) on
      one indented line does. Same shape as the elf.c fix for
      `const ulong stack_top = ...`.

  H3. Open `(` at end of name-line, all args on one indented line,
      `)` on its own line aligned with statement start:
        lhs = func(
           arg1, arg2, arg3, arg4
        );
      Use when H2 doesn't fit but all args fit on a single indented
      line.

  H4. Args spilling across multiple lines. Sub-ranking applies (Q1):
      - H4a (best): Style 2 strict -- one arg per line, `);` on own
        line. Documented as Q1 V2.
      - H4b: Style 1 one-per-line -- args aligned under opening
        paren. Documented as Q1 V4.
      - H4c: Style 1 packed -- multiple args per wrapped line.
        Documented as Q1 V1.
      - HARD-NO: one-arg-per-line with `);` glued to last arg
        (Q1 V3).

**Modeling implication:** the v2 prettiness score for a call-site is
not a single ranking but a CASCADE. Each level scores higher than the
next; the active level is selected by what fits given the surrounding
context (line width, arg count, indent depth). Score function walks
the hierarchy top-down and returns the highest-fitting form's score.

**Linter implication:** levels H2 and H3 are positive shapes
(legitimate corpus precedent). Existing `multiline_call_style` does
not flag them (good). When the rule fires on H4c-style violations,
the suggested alternative should be the HIGHEST-fitting form (H1 if
possible, else H2, etc.) -- not just "use Style 2 strict."

## Higher-order alternatives

### Init-with-default + conditional override

Source: Q5 (2026-05-20), user-volunteered.

When you're tempted to write a multi-line ternary OR an if/else that
sets the same variable to one of two values, consider this third
form:

```c
   int val = common_choice;       /* the usual case */

   if (choice != expected)
      val = other_choice;
```

This works when **the default value is cheap to evaluate** (no
side effects, no expensive call, no allocation). It dominates both
the ternary and the if/else when applicable, because:

- The decl line carries the "what is this variable usually" semantic
  anchor for the reader. The most common case is immediately visible.
- The override is visually subordinate -- short, single-statement,
  only mentions the deviation.
- The symmetric-but-heavy `if (cond) ... else ...` framing
  disappears entirely.

The user generally tries to avoid full if/else when this pattern
applies.

**Linter implication:** when the v2 model encounters a multi-line
ternary OR a 2-arm if/else that assigns the same LHS in both arms,
it should check whether one arm is "trivially cheap" (constant
literal, simple field access, etc.) and -- if so -- suggest the
default+override rewrite. This is a *rewrite suggestion* (level c
of the original end-state options), not a flat ranking entry.

## Asymmetries

### Function calls vs function definitions: different Style 2 rules

Source: Q1 (calls) + Q4 (defs), 2026-05-20.

For a multi-line **function call** with one arg per line, Style 2
strict (`);` on its own line) is the *preferred* form.

For a multi-line **function definition** with one arg per line,
Style 2 in any form is a *hard-no*; Style 1 (args aligned under the
opening paren) is the only acceptable shape.

The asymmetry is structural: in a call, `);` provides statement-
level closure that benefits from being on its own line; in a
definition, the `{` of the body already provides closure, so a
separate `)` line just creates two adjacent single-character lines
without earning its keep.

**Linter implication:** `multiline_call_style` should branch on
`CALL_EXPR` vs `FUNCTION_DECL` cursors and apply different rules:

- Call: `);` MUST be on its own line when each arg is on its own
  line (the existing v1 narrow Style 3 detector should be widened
  per Q1's hard rule).
- Def: open `(` MUST be on the same line as the function name;
  Style 2 (open `(` at end of name line, args indented +3) is
  forbidden regardless of whether `)` is on its own line or glued.

These are two separate rules with different cursor kinds.

## Naming patterns

### Tight-scope aliases: 1-2 letter abbreviation of purpose

Source: Q6 (2026-05-20), confirmed corpus pattern.

For a local alias that exists only within a small block (~5 lines or
fewer), the name should be a **1-2 letter abbreviation of the
variable's purpose**, not of its type and not with a descriptive
suffix. Examples from the corpus: `ti` (task), `pi` (process), `h`
(handle), `c` (conn), `ct` (curr ticks), `st` (selected ticks).

Order of preference (for the same alias):

  1. Single-letter initial of purpose -- `c` for conn, `h` for handle.
  2. Multi-letter abbrev / type-tag-matching name -- `conn`, `cn`.
     Acceptable as a second-best; the type-tag shadowing is fine.
  3. Descriptive name with `_ptr` (or similar) suffix -- `conn_ptr`.
     Anti-pattern. Verbose without earning its keep in tight scope.

This applies only to TIGHT scope. As scope widens, longer names
become preferable to keep the referent clear at distance.

## Hard rules surfaced by ranked-preference questions

These are rules promoted from "soft preference" to "hard rule" by user
feedback during the v2 ranked-preference loop. They should be enforced
by `style_check`, not just penalized.

### Call-site layout: no one-arg-per-line with `);` glued to last arg

Source: Q1 (2026-05-20).

When a multi-line function call places each argument on its own line,
the closing `);` MUST be on its own line aligned with the statement's
opening column. The "Style 2 hybrid" form (args one-per-line, `);` at
end of last arg) is unacceptable. The symmetry of `);` on its own line
is what makes Style 2 readable; without it, the call has no visual
closing-paren anchor.

**Distinguishing case** -- when args *themselves* wrap with sub-
expressions (e.g. ternaries spread across two physical lines per
logical arg, as in `userapps/tracer/screen_tracing.c:196-210`), `);`
glued to the last *line* is the unavoidable shape and not the same
rule violation. The hard rule applies only when each argument fits on
exactly one line of its own.

**Linter implication:** `multiline_call_style` currently catches only
the case where all args sit on ONE indented line (close_line ==
open_line + 1). Extend it to also flag the multi-line-args /
end-of-last-arg-`);` shape, while distinguishing the legitimate
inner-wrapping case.

---

## Cross-cutting observations

### Extraction cost grows fast with local count and name blandness

Source: Q2 (2026-05-20).

When the choice is between (a) extracting N sub-expressions to named
locals, (b) introducing a single convenience pointer that flattens
the parent reference, or (c) accepting brace-on-own-line, the
preference order is **(b) > (c) > (a)** even when N is as small as 2.

Two cost factors making (a) lose to (c):

1. **Local count cost**: each extracted local adds to the function's
   local-count budget, which is a per-function prettiness penalty.
   N=2 is already enough to tip (a) below (c) when the alternative
   is one layout artifact (brace-on-own-line).
2. **Semantic anchor cost**: extracting `sess->conn->state` to a
   bare `state` local loses the semantic chain. The reader now has
   to look up the decl to know what `state` is, whereas the inline
   form was self-describing. This is amplified when the leaf field
   names are generic (`state`, `kind`, `count`).

(b) wins because it is the minimal-disturbance refactor: the leaf
references retain their original names, only the parent is renamed
to a short alias. The semantic chain is preserved.

**Linter implication:** when the v2 prettiness model considers an
extraction, the cost should include (i) a per-local penalty that
accumulates against the function's local budget, and (ii) a
"name-genericness" penalty that's higher when the proposed local
name is short/generic (a tunable feature, hard to evaluate
mechanically -- start with: penalty rises sharply for names <= 5
chars).

### const is monotonically valuable; no consistency penalty

Source: Q3 (2026-05-20).

Adding `const` to a declaration where it is correct is a *gain*;
missing it is a *loss*. There is NO penalty for applying const to
some declarations but not others within the same block -- partial
const strictly beats no const. The "consistency" intuition (that
mixed const usage looks half-committed) is wrong.

The previously-recorded "visual weight" exception still applies:
when the type spelling is heavy enough (e.g. `enum task_state`,
~15+ chars), const tips the decl from "annotated" to "noisy" and
can be skipped. But this is a per-decl judgment based on TYPE
length, not a function-wide consistency consideration. The
threshold is somewhere between short scalars (`int`, `bool`,
`u32`) where const is free, and long enums/multi-word types where
const adds noise.

**Linter implication:** per-decl, a missing const where it COULD be
applied adds a small negative coefficient. The penalty is
type-length-conditional: zero (or near-zero) below some character
threshold, larger above. This is additive across decls, not
modulated by within-block consistency.
