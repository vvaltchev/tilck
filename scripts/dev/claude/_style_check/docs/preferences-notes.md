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

## Choice-of-form dimensions

### Macro vs inline: type-genericity and compiler-inlining

Source: Q16 (2026-05-20).

The default preference for small reusable computations is:
**V2 (static inline) > V1 (macro) > V3 (regular fn)** for a
single-type expression. Two dimensions shift the ranking:

**Dimension 1 -- type genericity.** C has no templates. If the
same expression must work across N integer types, an inline
function must be either duplicated N times or wrapped in a
`_Generic` / X-macro dispatch layer (e.g. the
`atomic_load`/`atomic_store` setup in
`include/tilck/common/atomics.h`).

  - N = 1: V2 dominates (type safety, zero macro pitfalls).
  - N = 2: V1 ~= V2 (V2 with two inline impls is close to V1).
  - N >= 3: V1 dominates (a single generic macro beats N
    duplicates or a dispatch layer's complexity).

**Dimension 2 -- compiler-inlining failure.** When `-O3` does
NOT inline a `static inline` function despite the keyword
(observed in terminal code), the runtime cost becomes real.
V1 is then the forced choice. The user accepts this AS LONG AS
the deviation is documented:

  - Add a comment at the macro definition explaining why the
    function form was tried and abandoned.
  - The macro is a documented exception, not a default.

**Linter implication:** when v2 sees a macro definition,
ranking depends on:
  - how many types it must support (count distinct types in
    expansion-site usage, hard to compute mechanically; rough
    proxy: look for type casts in the macro body or count
    distinct integer types in callers),
  - whether the macro has a comment near its definition that
    looks like a "why this is a macro" justification.

Without both signals, default to a soft penalty: "consider
static inline instead of this macro." With either signal
present, the penalty should drop or vanish.

### Documenting pragmatic exceptions

Source: Q16 (2026-05-20) -- generalized.

When code intentionally violates the user's stated default style
preference for pragmatic reasons (compiler limitations, hardware
quirks, ABI requirements, perf-critical hot paths), the deviation
MUST be accompanied by a comment that explains:

  - WHAT the default preference would have been.
  - WHY the deviation is needed in this specific case.

Examples observed:
  - Q16: macro instead of inline because `-O3` didn't inline.
  - Q11: `DEBUG_ONLY()` wrap accepted as necessary evil over
    cleaner `if (DEBUG_CHECKS)` -- documented in CLAUDE.md.

This is a meta-rule: it doesn't tell the linter what to
enforce in code shape, but it tells the linter NOT to flag a
deviation as drift when an explanatory comment is present
within ~5 lines.

**Linter implication:** when a hard-rule violation is detected,
scan a small window above the violation for a comment. If a
"why this exception" comment is present, downgrade the
violation to a soft warning (or suppress entirely). Without
the comment, treat as drift.

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

## Concrete formatting rules surfaced

### Free functions must accept NULL (no guards before free)

Source: Q11 (2026-05-20).

All `free_*` functions in this codebase are required to be no-ops on
NULL input. As a consequence, cleanup code (especially under a
goto-out label) **never** needs an `if (ptr)` guard before the call.

```c
out:
   free_bar(b);   /* b may be NULL -- free_bar handles it */
   free_foo(a);   /* a may be NULL -- free_foo handles it */
   return rc;
```

Code that includes such guards has an *idiom error*: it betrays a
misunderstanding of the free contract or an over-defensive habit
from libraries that don't have this guarantee.

**Linter implication:** detect `if (ptr) free_X(ptr);` patterns and
flag them as candidates for unguarded calls. Need to know which
identifiers are project free functions (heuristic: any function
matching `*free*`, `kfree*`, or that takes one pointer arg and
returns void -- with allowlist tuning).

### Multi-line boolean operators (`||`, `&&`) are column-aligned

Source: Q10 (2026-05-20), user-volunteered.

When a boolean expression wraps across multiple lines, the connecting
operators (`||`, `&&`) should be COLUMN-ALIGNED across the wrapped
lines. Pad with extra space before the operator if needed.

```c
   if (ti->state == TASK_STATE_SLEEPING ||
       ti->state == TASK_STATE_STOPPED  ||      /* extra space to align */
       ti->state == TASK_STATE_TRACED)
```

Same principle as `#define` value alignment, struct field comment
alignment, and ternary `?` / `:` alignment: visual alignment is a
form of prettiness in this codebase. Not a hard rule yet (no
explicit "this is a hard rule" statement), but a clear preference.

**Linter implication:** detect multi-line boolean expressions and
verify the operator columns match. Penalty applied per misaligned
operator. Cheap to detect once the multi-line expression is
identified.

## Hard rules surfaced by ranked-preference questions

These are rules promoted from "soft preference" to "hard rule" by user
feedback during the v2 ranked-preference loop. They should be enforced
by `style_check`, not just penalized.

### Helper placement: above the caller, in the right file

Source: Q14 (2026-05-20).

Helper-function placement is two decisions stacked.

**Decision 1 -- which file?** Each function should live in the file
where it conceptually belongs:

  - Truly private to a single caller, no broader interface ->
    in the same .c as the caller (as `static`).
  - Part of a broader interface used by multiple callers or
    multiple files -> in the appropriate .c with a declaration
    in the matching .h. "Each function should be placed in the
    right file, not at random."

Claude's bias: defaulting to "same file as the current caller" for
all helpers. That's wrong when the helper has conceptual
applicability beyond one site.

**Decision 2 -- where in the file?** When a helper lives in the
same .c as its caller, place it ABOVE the caller. Forward
declarations to allow caller-above-helper layout are forbidden
unless mutual recursion (or another language-imposed forcing
function) makes the forward decl genuinely unavoidable.

  - Helper above caller: preferred.
  - Caller above helper with forward decl: HARD RULE violation
    if reordering would work. "Placing a helper below its caller
    is very bad."

**Linter implication:** detect `static FUNC_DECL` forward
declarations at the top of a file. For each, check whether the
corresponding definition appears below ALL its callers. If yes
AND the call graph has no mutual recursion involving this
function, flag as a forward-decl-avoidance violation.

### `#include` ordering: tilck_gen_headers first, then grouped by subtree

Source: Q13 (2026-05-20).

`#include` ordering at the top of a Tilck .c file has two structural
constraints (hard rules) and one soft preference:

  HARD: `tilck_gen_headers/...` MUST come first. It carries the
  generated config defines that other headers in the project
  depend on; putting it anywhere else breaks the build or
  silently mis-resolves config.

  HARD: includes MUST be grouped by subtree (gen_headers /
  common / kernel / mods / 3rd_party / system). Strict
  alphabetical ordering across subtrees is forbidden because it
  inevitably violates the "gen_headers first" rule.

  SOFT: insert a blank line between subtree groups (preferred)
  vs single contiguous block (acceptable). The blank-line form
  makes the grouping visually explicit.

**Linter implication:** detect the `#include` block at file
top. Verify the first directive is `<tilck_gen_headers/...>`
and that the remaining directives are subtree-grouped (no
group interleaving). Blank lines between groups are a soft
preference -- penalty per missing separator, not a hard
violation.

### Switch-case: case labels indented +3 from switch (no Linux-flush)

Source: Q12 (2026-05-20).

Case labels MUST be indented 3 spaces from `switch`; case bodies
MUST be indented 3 spaces further (total 6 from switch). The
Linux-kernel style of `case` flush with `switch` is forbidden.

```c
   switch (op) {
      case OP_READ:        /* case at +3 */
         rc = do_read();   /* body at +6 */
         break;
      ...
   }
```

**Linter implication:** detect SWITCH_STMT cursors and verify each
case label's column matches `switch_col + 3`. The Linux-flush
style is a hard violation, not a soft preference.

### Switch-case: per-case braces mandatory when case has locals

Source: Q12 (2026-05-20).

When a case body declares its own locals, the case MUST be braced:

```c
   case OP_READ: {
      int len = ...;     /* per-case local */
      rc = do_read(len);
      break;
   }
```

Without braces, the local declaration is technically broken in C
(case labels don't introduce scope) -- so the braces are required
by language semantics, not just style. The choice "brace this case
or not" reduces to "does this case have its own locals."

**Linter implication:** for each CASE_STMT cursor, check whether
its body contains VAR_DECL. If yes AND no braces, flag as
violation. If no locals, prefer the unbraced form (braces in this
case are stylistic noise).

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

### Locals at top of *enclosing scope* (not just function-top)

Source: Q15 (2026-05-20). REVISES the earlier dropped-rule note.

**Non-const locals MUST be declared at the top of their enclosing
SCOPE.** That scope can be:

  - The function body (C89 top-of-function style), OR
  - A control-flow body (`if`/`for`/`while` body), OR
  - An explicit `{ ... }` sub-block deliberately introduced to
    narrow the local's visibility.

C99 mid-block declarations (decl interleaved with statements at
the same brace depth) are UGLY and should be avoided. The
corpus contains such cases (kernel/signal.c:21-33,
kernel/poll.c:21, kernel/sched.c:443) but the user has confirmed
these are unpolished/rushed code, not a style endorsement.

**Important reversal of an earlier wrong call.** In an earlier
note in this file I documented `non_const_locals_top_of_block` as
having been "dropped from the linter because the corpus
contradicts the hard-rule reading." That inference was wrong: the
rule documented in CLAUDE.md is the rule; the corpus drift is the
violation. The linter rule should be re-enabled (with the
sub-block scope-narrowing exception correctly implemented).

**The sub-block is the escape valve.** When a local is only
relevant to part of the function, the right move is NOT to
declare it mid-block but to wrap that part in `{ ... }` and
declare at the top of the sub-block. This keeps the
"top-of-scope" rule intact while narrowing the local's lifetime.

**Escalation when the sub-block grows.** If the sub-block becomes
"too large" (no precise threshold yet; rough guidance is when the
sub-block becomes harder to scan than a small standalone
function) OR if it would need its own nested sub-block, EXTRACT
the sub-block's content to a helper function instead of nesting.

**Linter implication:** re-enable `non_const_locals_top_of_block`
with the corrected semantics:

  - Walk every COMPOUND_STMT cursor.
  - Within each, find all VAR_DECL children that are non-const.
  - For each, check whether any non-DECL_STMT child precedes it
    in the same block. If yes -> violation (the local is being
    declared after non-declaration statements in its scope).
  - This naturally allows the sub-block escape valve: the local
    in the sub-block has the sub-block as its enclosing
    COMPOUND_STMT, not the function body.

### Extraction calculus: savings > cost

Source: Q2 + Q10 (2026-05-20), reconciled.

The rule for "should I extract this expression to a local?" is a
trade between savings and cost:

  **savings** = number of use sites where the new local replaces a
                repeated expression.

  **cost**    = 1 per new local introduced, plus a small surcharge
                if the proposed local name is short / generic (the
                "name blandness" cost).

Extract when **savings > cost**.

Concrete cases observed:

- **Q2** (2 different fields, each used once): cost 2, savings 2.
  Net ~zero, plus name-blandness penalty -> extract LOSES to
  brace-on-own-line. The convenience-pointer alternative (1 local,
  shortens 2 different references) flips the math: cost 1, savings
  2 -> WINS.

- **Q10 V2** (1 field used 3 times): cost 1, savings 3 -> extract
  WINS even at a single call site, beating brace-on-own-line.

(b) above (convenience pointer) wins in Q2 because it is the
minimal-disturbance refactor: the leaf references retain their
original names, only the parent is renamed to a short alias. The
semantic chain is preserved.

**Linter implication:** the v2 prettiness model's "extract-to-local"
suggestion should weigh (i) per-local cost, (ii) name-blandness
surcharge (penalty rises for names <= 5 chars), and (iii) use-site
count at the proposed extraction's location. Only suggest extract
when savings strictly exceed cost.

### Error-handling shape: context-dependent cascade

Source: Q11 (2026-05-20).

There is no single "best" error-handling shape; the preferred form
depends on the function's overall size and the number of resources
that need cleanup.

  Function shape                  | Preferred shape
  --------------------------------|-----------------------------
  Very short body (the work       | Nested ifs (V1). Elegant
    function is literally one     | when there's effectively one
    call: `rc = work(a, b)`)      | level of nesting and no
                                  | duplication exists yet.
  Medium fn, exactly 2 resources  | Either goto-out (V3) or
                                  | early-return-with-duplicate-
                                  | cleanup (V2). V2 is a bit
                                  | nicer for avoiding the goto;
                                  | the choice is close.
  Long fn, 3+ resources           | goto-out (V3) dominates.
                                  | Consolidated cleanup at a
                                  | single label is the kernel
                                  | idiom.

**Meta-rule for Claude:** when in doubt about which shape to use,
pick goto-out (V3). It is never *wrong* even when V1 or V2 would
be slightly nicer. The other two have narrower applicability
windows.

**Linter implication:** the v2 prettiness model should score
goto-out as the always-acceptable baseline (small fixed
penalty for the goto itself), while V1 and V2 each carry a
context-conditional bonus that activates only in their narrow
applicability windows. Without per-function context, the safe
ranking is V3 > V2 > V1.

### Helper-function amortization

Source: Q10 (2026-05-20).

Introducing a new helper function (e.g., `task_is_blocked()`)
imposes a fixed one-time cost: the helper's definition adds lines,
and the helper becomes a name to maintain.

This fixed cost is amortized across call sites:

- **1 site**: helper is overkill; inline or extract-local wins.
- **2 sites**: helper ties with extract-local; close call.
- **3+ sites**: helper dominates strongly. Cleaner call sites, the
  conceptual identity ("is blocked") becomes named, the
  duplication is gone.

The cost shape is different from extract-local (which has a per-use
cost) -- helpers have a per-helper fixed cost plus a tiny per-call-
site cost (the call itself, vs. the inline expression).

**Linter implication:** when 3+ near-identical OR-chains exist on
the same operand pattern, suggest extracting them to a single
helper. Below that threshold, suggest extract-local or do nothing
based on the extraction-calculus rule above.

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
