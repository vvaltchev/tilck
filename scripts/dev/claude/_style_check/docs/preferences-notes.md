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

  H4. Args spilling across multiple lines. Sub-ranking applies (Q1)
      WITH a callee-length-conditional twist surfaced in Q30:

      For SHORT callees (~<= 10 chars, e.g. `printk`):
        - H4a (best): Style 1 -- args aligned under opening
          paren. Q1 V4 form (one arg per line) or Q1 V1 form
          (packed) depending on density.
        - H4b: Style 2 strict -- `(` ends line, args +3
          indented, `);` own line.

      For LONG callees:
        - H4a (best): Style 2 strict -- `(` ends line, args
          +3 indented, `);` own line. Doesn't waste horizontal
          space on alignment.
        - H4b: Style 1 -- valid but indents args too far right.

      Common across both:
        - HARD-NO: one-arg-per-line with `);` glued to last arg
          (Q1 V3 form).

      The threshold: at what column does the opening paren land?
      If that column is past ~25-30 from the line start,
      Style 2 wins (callee is "long"). Otherwise Style 1 wins
      (callee is "short").

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

## Claude-process rules (distinct from linter rules)

Source: emergent across Q14 (placement), Q16 (DEBUG_ONLY),
Q19 (naming-content out of scope), Q28 (comma-expression
macros).

A category of rule that governs what CLAUDE DOES when
generating new code, distinct from what the LINTER should
flag in existing code. Existing code that violates a
Claude-process rule is NOT rewritten as a violation; but
Claude must not produce such code from scratch.

Examples:

  - **Don't generate comma-expression macros** (Q28). V3 of
    Q28 (`#define X(p, v) ((*(p) = (v)), (p)++)`) is too
    subtle. Existing instances are accepted; new Claude
    output must not introduce them.

  - **Don't enforce identifier-content choices** (Q19). The
    tool should not penalize specific identifier-content
    patterns (e.g. "prefer `is_X` over `X_is`"); semantic
    naming decisions are out of scope.

  - **Don't reach for `DEBUG_ONLY()` when `if (DEBUG_CHECKS)`
    works** (CLAUDE.md). DEBUG_ONLY() is a necessary evil
    when the debug-only code needs a declaration; otherwise
    the `if (DEBUG_CHECKS) { ... }` form is preferred. This
    is a process rule: existing DEBUG_ONLY() that COULD have
    been an `if (DEBUG_CHECKS)` is not rewritten.

  - **Place helpers in the right file, not at random**
    (Q14). A semantic placement decision rather than a
    syntactic one.

**General principle:** when a rule depends on intent,
semantics, or context that the linter cannot mechanically
determine, it belongs in this process-rules category. The
v2 linter should not try to enforce these. Claude's own
behavior is the place these rules live, and they survive
across sessions via memory entries and CLAUDE.md.

## v2 tool scope

### What the tool should and should not try to enforce

Source: Q19 (2026-05-20), user-volunteered scope boundary.

The v2 tool's domain is **cosmetic / formatting / layout /
structural** choices about code shape. It should NOT try to
enforce semantic decisions about identifier content.

**In scope (cosmetic/formatting):**

  - Layout (indentation, brace placement, wrap style, alignment)
  - Whitespace (blank lines, trailing whitespace, operator spacing)
  - Comment shape and placement (prologue vs inline, alignment)
  - Const usage and qualifier placement
  - Structural choices (sub-blocks, helper extraction thresholds)
  - Name SHAPE (length conventions, suffix patterns -- e.g. Q6's
    `c` vs `conn` vs `conn_ptr` choice for an alias)

**Out of scope (semantic):**

  - Identifier CONTENT choices (subsystem prefix to use, verb to
    pick, conceptual carving of a name) -- e.g. Q19's
    `task_is_runnable` vs `is_task_runnable` decision.
  - Whether a particular abstraction is the right one.
  - Whether a function's behavior is correct.

The line between "name shape" and "name content" can be blurry
in practice. Rule of thumb from the user: "if it requires
understanding the semantics of the code to answer, it's our job
(human + Claude in conversation), not the tool's job."

**Linter implication:** v2 should not include rules that try to
penalize specific identifier-content patterns (e.g. "prefer
`is_X` over `X_is`"). It MAY include rules about identifier
shape that are syntactically detectable without semantic
understanding (e.g. "single-letter aliases acceptable in scopes
of <= N lines"). Even the latter should be soft suggestions, not
hard rules.

## Meta-principles

### Restructure rather than scaffold

Source: emergent across Q11, Q14, Q15, Q17 (2026-05-20).

When code's shape resists the natural commenting or structural
choice, the user's recurring response is to RESTRUCTURE rather
than add scaffolding. Concrete instances of this principle:

  - Function name doesn't convey purpose  → rename it; don't add
    a one-liner comment that paraphrases (Q17).
  - Sub-block grows too large or needs    → extract to a helper
    nesting                                  function (Q15).
  - Helper used by multiple callers in    → move to the right
    different files                          file/header (Q14).
  - Implementation comments make the      → split the function
    function too long                        (Q17).
  - Multi-line ternary or symmetric       → rewrite as init-with-
    if/else when default is cheap            default + override
                                             (Q5).

The pattern: rather than tolerate a deformed code shape, find
the restructure that makes the shape natural. Comments,
forward declarations, mid-block decls, and verbose names are
all forms of scaffolding propping up a shape that should have
been changed.

**Important counter-balance (Q32):** not every restructure is
good. Splitting also requires justification. A function in the
20-30 line range with cohesive inline phases is at the right
size; splitting it into helpers each used by only one caller
is over-engineering. Extract a helper only when:

  - The sub-piece has a name worth giving, AND
  - The sub-piece is REUSABLE (multiple callers, or clear
    likelihood of future reuse).

Without both signals, "extract the sub-block to a helper" is
the wrong response to "this function is getting long." A long
function that does ONE cohesive thing is fine. Over-splitting
(too many helpers, each used once) is as bad as 500+ LoC
monoliths -- both anti-patterns at opposite extremes.

**Linter implication:** when the v2 model detects a violation,
the suggestion should often be a RESTRUCTURE rather than a
direct rewrite of the violated shape. Examples:

  - One-liner-prologue-comment detected: suggest improving the
    function name, not removing the comment.
  - Deep sub-block detected: suggest extracting to a helper, not
    flattening the sub-block.
  - Mid-block decl detected: suggest moving it to the top of a
    new sub-block (introducing scope narrowing) OR extracting
    the surrounding statements to a helper, not just hoisting
    the decl to the function top.

### Function name carries purpose

Source: Q17 (2026-05-20).

The function name is the PRIMARY documentation channel for what
the function does. Comments are reserved for what the name CAN'T
carry: interface contracts (return-code semantics, preconditions,
ownership), implementation rationale, and non-obvious invariants.

A comment that just paraphrases the function name is wrong on two
counts:

  - It adds no information beyond the name.
  - It implies the name is insufficient, which means the name
    should have been improved instead.

**Linter implication:** detect prologue comments that closely
mirror the function name (high token overlap, no return-code
detail, no precondition detail). Flag as candidates for either
removal (if function is simple enough that the name is
self-explanatory) or expansion (if the comment is trying to do
real work but doing it poorly).

### Interface comments vs implementation comments live in different places

Source: Q17 (2026-05-20).

Two distinct concerns, two distinct comment placements:

| Concern                         | Where                |
|---------------------------------|----------------------|
| Interface (purpose, return-     | Prologue (above fn)  |
| codes, preconditions, ownership)|                      |
| Implementation (algorithm,      | Inline (inside fn,   |
| invariants, tricky reasoning)   | at the relevant code)|

Mixing them is wrong shape. A prologue trying to explain how the
function works is putting commentary in the wrong place. An
inline comment trying to document the function's interface is
inviting drift (callers don't read inline comments).

**Linter implication:** when a prologue block-comment contains
implementation-style language ("we first ...", "this loop ...",
"the trick is ..."), suggest moving that content inside the
function body adjacent to the code it explains.

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

## Semantic-shape principle

### Code form matches conceptual shape

Source: Q22b + Q26 (2026-05-20).

A recurring principle across questions: the SHAPE of code should
match the conceptual SHAPE of what it expresses. Same rules in
two manifestations:

- **Q22b -- per-level symmetry.** `call(foo(1,2,3), bar(1,2,3))`
  works because foo and bar are PEERS at the same conceptual
  level, so their spacing form matches. Mixing compact and
  non-compact within one level breaks the conceptual peerage.

- **Q26 -- error-precondition validation form.** Combined OR
  check (V1) groups validations under "any precondition
  failed." Sequential early-returns (V2) make each validation
  a visible step of its own. The right form depends on whether
  the conditions are semantic peers or heterogeneous kinds:

  Heterogeneous (null + flags + state + refcount -- different
  KINDS of validations): V2 wins, because each check is its
  own conceptual thing and deserves its own visible name.
  Lumping them under one OR is "mix-and-match" and not nice.

  Homogeneous / semantically related (all range checks, all
  auth checks, all membership checks): V1 wins, because the
  OR is the natural grouping of peers under one umbrella.

**Linter implication:** the semantic-shape principle is
inherently fuzzy. The tool cannot mechanically determine whether
two conditions are "semantically related." The right move is to
emit BOTH forms as suggestions when V1/V2 are structurally
equivalent for a multi-clause condition, with the choice text:
"V2 if conditions are heterogeneous (different kinds); V1 if
they are homogeneous / semantically related."

This is a case where the v2 model produces a *suggestion list*
rather than a single recommendation. The model's job is to
identify the choice point; the human (or future Claude making
the call) selects based on semantic judgment.

## Control-flow preferences

### Prefer `return` over `goto` when restructuring allows it

Source: Q35 (2026-05-20).

`goto` is canonical for cleanup-at-out (Q11) when there are
resources to release across multiple failure points. Outside
of that, the user prefers to RESTRUCTURE the function so the
control flow can use `return` directly:

  - `goto`-out-of-nested-loops -> extract the loop body (or
    the entire search) to a function that `return`s on the
    match condition. Caller checks the return value.

  - `goto`-for-error-handling beyond cleanup -> usually a sign
    the function is doing too much; split it.

The remaining canonical `goto` use case is multi-resource
cleanup (Q11 V3): one goto target, releases everything,
returns. Single-purpose, well-bounded.

**Linter implication:** detect `goto` labels and check whether
they are followed only by cleanup-shaped code (frees /
unrefs / unlocks). If a goto label is followed by
substantive post-loop processing OR the goto exits nested
loops just to do more work, flag as a refactor candidate
(soft suggestion -- restructure to use `return`).

### Prefer return-value over out-param for "find" / "compute" functions

Source: Q35 (2026-05-20).

A function that finds or computes something should RETURN
that thing (or NULL / sentinel if not found / undefined).
Out-params force the caller to maintain pre-declared state
that the function then mutates, which adds friction at every
call site.

Out-params remain appropriate when:
  - Multiple values genuinely need to flow back (and a
    struct return doesn't fit), OR
  - The function returns a status code (`int rc`) and the
    "result" is secondary.

But the default for "find X" / "compute X" / "lookup X" is:
return X, not (status, X-via-out-param).

**Linter implication:** detect function signatures of the
shape `static int find_X(args, T **out)` and flag as a
candidate for refactor to `static T *find_X(args)`. Soft
suggestion -- the linter can't always tell whether the
existing out-param pattern is justified.

## Process notes

### Preferences can evolve through the loop

Source: Q39 -> Q46 revision (2026-05-20).

The ranked-preference loop itself can sharpen the user's
underlying intuition about a style choice. At Q39 the user
ranked `_t` first as the function-pointer typedef suffix; at
Q46 (a related but different question about function-pointer
fields) the user revised: `_func` is actually preferred, and
`_t` is reserved for value-type typedefs.

This isn't a contradiction or an error -- it's the natural
evolution of an articulated preference. The Q39 answer was
true for the user's intuition at the time; Q46's framing
sharpened it.

**How to handle revisions:** when the user explicitly asks to
update an earlier answer, update the YAML entry directly with
a clear note of the revision date and triggering question.
The history is in commit messages, so the YAML file itself
should reflect the current best understanding. Do NOT keep a
stale ranking with a separate "but actually..." entry --
that confuses future fitting.

**Meta-principle:** treat `preferences.yaml` as "recorded
thought as of [date]" rather than immutable history. Allow
revisions when prompted; mark them clearly in the affected
entry's `comment` and `notes` fields.

## Layout serves usage

### Choose ordering axis based on the lookup pattern

Source: Q45 (2026-05-20).

For collections with multiple plausible organizing axes
(e.g. semantic categories AND numeric value, or alphabetical
AND grouping by purpose), the right axis is the one that
matches how the collection will be *searched*. Examples:

- **Flexible-value enum (numbers reassignable):** group by
  semantic category, then assign sequential per-group values.
  This serves "find SYS_OPEN in the file-I/O group" while
  also keeping numbers monotonic.

- **Fixed-value enum (ABI / protocol numbers):** order by
  numeric value. This serves "find the entry for value 42"
  and "spot a missing value in a range" -- the two main
  searches when numbers are externally meaningful.

- **`#include` list (Q13):** group by subtree (gen_headers,
  common, kernel, mods). Searches are by source layer.

- **Struct field order:** group fields related to the same
  sub-aspect together. Searches are by purpose.

**General principle:** prettiness isn't only visual rhythm
or conceptual fit; it also serves the *intended lookup
pattern of the consumer.* When two axes compete (semantic
grouping vs numeric order), pick the one that matches how
people actually find things in this collection.

**Linter implication:** the linter can't reliably determine
lookup pattern from source alone (ABI-fixed vs flexible is
a semantic distinction). Surface both options as soft
suggestions, deferring the final choice to the human.

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

### Compact-form-for-line-fit (with symmetry requirement)

Source: Q22 (2026-05-20).

A cross-cutting space-omission rule. The default spacing rules
in this codebase are:

  - **Cast:** `(Type *)expr` -- space inside (between Type and
    `*`), NO space after the closing `)`.
  - **Argument lists:** `foo(a, b, c)` -- space after each comma.
  - **Binary operators:** `a + b` -- space around the operator.

When a statement is *just* over 80 cols (~81 cols) AND wrapping
would make the code uglier, these spacing rules MAY be tightened
*uniformly* across the whole expression to fit on one line:

  - Cast: `(Type*)expr` -- drop space inside AND after `)`.
  - Argument lists: `foo(a,b,c)` -- drop comma-after spaces.
  - Binary operators: `a+b` -- drop spaces around operator.

**Symmetry rule (hard) -- PER NESTING LEVEL, not global.**
Compactness applies to each argument-list / parenthesized group
independently. Within one level, all peers must use the same
form. The decision does NOT propagate up or down the nesting
tree.

```c
call(foo(1,2,3), bar(1,2,3))       /* OK: inner level symmetric */
call(foo(1,2,3), bar(1, 2, 3))     /* FORBIDDEN: asymmetric inner */
something_else(call(foo(1,2,3), bar(1,2,3)), 42)
   /* OK: outer level (something_else's args) keeps default
    * spacing; inner level (call's args) decides separately. */
```

So the rule is "each level decides its own form; that decision
must be uniform across that level's peers." Mixing peers
asymmetrically within one level is the violation; mixing
LEVELS (default outside, compact inside) is normal and fine.

**Meta-rule -- compact form is a last resort.** The user prefers
to break the line and accept the wrap over deploying the compact
hack. Compact form should only appear when:

  - The wrap would be aesthetically worse than the compact line,
    AND
  - The single-line form fits in 80 cols after compaction,
    AND
  - All spaces in the expression can be tightened symmetrically.

When in doubt, wrap.

**Forbidden -- asymmetric "compact" forms.** A cast like
`(Type*) expr` (compact inside, normal outside) is forbidden.
It's the half-applied compact form, which has the worst of both
worlds: visually compact inside the cast, visually expanded
around it.

**Linter implication:** the rule has two faces.

  - Default mode: detect violations of standard spacing (cast
    no-space-after-`)`, args with spaces after commas, operators
    with spaces around them).
  - Compact mode: when the entire expression uses the compact
    form symmetrically AND the line ends at or under 80 cols,
    accept it as a "compact-form override" with a small
    soft-penalty (compact-form is a hack, not the default). When
    the same expression has mixed compact and default forms,
    flag as a symmetry violation regardless of line length.

### Visual harmony with surrounding lines

Source: Q22 clarification (2026-05-20).

The 80-column limit is a HARD upper bound, but staying under it
is not sufficient for prettiness. Lines that DO fit in 80 cols
may still need to be wrapped if they stand out from the visual
rhythm of the surrounding region.

**Rule:** "If there is too much difference between the length of
a line and the one below... break the line even if it does fit
in 80 columns, simply because visually the code would look ugly.
There must be harmony."

Concrete consequences:

  - A 79-col line surrounded by 30-40-col lines is *ugly* even
    though it complies with the column limit.
  - A small block where every line is roughly the same width is
    pretty; a block with one outlier-long line is not.
  - Wrapping decisions should consider the local neighbourhood,
    not just the line in question.

**Meta-rule for compact-form-vs-wrap:** when in doubt between
"squeeze with compact form to fit 80" and "wrap to maintain
harmony," prefer the WRAP, "in a beautiful way." Compact form
is a save-the-line hack; wrapping for harmony is style.

**Linter implication:** this rule is fuzzy and hard to encode
mechanically. A v2 heuristic: for each line in a basic block,
compute the distribution of line lengths. Flag lines whose
length deviates significantly from the local mean as
"harmony-violations" -- a soft penalty, not a hard rule. The
threshold and the local-window definition are tunable. This
is the kind of rule where the v2 model should emit a hint
(level c -- a suggested alternative) rather than a hard
violation.

### Pointer `*` attached to the variable name (hard rule)

Source: Q21 (2026-05-20).

Pointer declarations and parameters MUST use the `Type *var` form:
a single `*` immediately preceding the variable name, with
whitespace BEFORE the `*` and no whitespace AFTER. Both
alternatives are hard-rule violations:

  - `Type* var` (attached to type) -- FORBIDDEN.
  - `Type * var` (spaces both sides) -- FORBIDDEN.

The same shape applies to all pointer contexts: variable
declarations, function parameters, struct field declarations,
return types of pointer-returning functions, etc.

**Linter implication:** detect every VAR_DECL / PARM_DECL whose
type is a pointer. Walk the token stream between the type and
the variable name. The required pattern is:

  `<type tokens> WS+ * <name>`

i.e. whitespace before `*`, no whitespace after, name follows
directly. Any other shape is a violation. Likewise for return
types of pointer-returning functions and pointer field
declarations.

### Multi-line `#define` continuation backslashes go at column 80

Source: Q20 (2026-05-20).

For a function-like macro whose body spans multiple lines, the
`\` line-continuation characters MUST all sit at column 80 (or
just within it). This gives visual consistency across every
macro in the project, regardless of body width.

```c
#define INIT_STATE(s)                                                          \
   do {                                                                        \
      (s)->count = 0;                                                          \
      (s)->head = NULL;                                                        \
      atomic_store(&(s)->ready, true);                                         \
   } while (0)
```

An acceptable fallback (lower-scored): backslashes aligned to
the longest body line + small padding (the "body-aligned"
form). The corpus has examples of both -- the body-aligned form
appears in `include/tilck/kernel/sync.h:159-164`
(`STATIC_KSEM_INIT`) -- but the user has confirmed that's drift,
not the canonical.

**Forbidden** (hard rule): backslashes placed immediately after
each content line with a single space. This produces
inconsistent backslash columns within a single macro and is
rejected.

**Linter implication:** detect multi-line `#define` directives;
extract the column of each `\` continuation. Verify:

  - All backslashes within the same macro at the same column.
  - That column equal to 80 (preferred) or within a tolerance
    of the longest body line + a few spaces (fallback,
    lower-scored).

Per-line varying backslash positions is a hard violation.

### Blank lines inside function bodies

Source: Q18 (2026-05-20).

Three concrete rules:

1. **Blank line after the declaration block.** When a function (or
   sub-block) starts with a run of declarations, an empty line MUST
   follow the last decl before the first non-declaration statement.
   This is a hard rule, not a soft preference.

2. **Blank line after every `return` statement except the last
   one in the function body.** A return mid-body is the end of a
   logical chunk; the blank line separates it visually from what
   comes next. The final return at function end is followed by
   `}` and needs no blank.

3. **Group statements by topic with blank lines between groups.**
   General principle: blanks delimit logical sections the same way
   comment alignment delimits visual columns. Adjacent statements
   that operate on the same object / topic stay together; a
   blank line marks a topic change.

The dense "no blanks" form is **totally unacceptable** -- not
just less preferred. The minimal "only after decls" form is
**very ugly** -- strong preference against but not a hard
violation in itself.

**Linter implication:**

  - Rule 1 is mechanically detectable: find each COMPOUND_STMT
    cursor; check whether the first non-DECL_STMT is preceded
    by a blank line.
  - Rule 2 is mechanically detectable: for each RETURN_STMT
    cursor that is not the last statement in its enclosing
    COMPOUND_STMT, check that the next physical line is blank.
  - Rule 3 (topic grouping) is fuzzier; defer to a v3 heuristic
    that could use object-of-reference changes between
    statements as a signal.



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

### Multi-line boolean condition: operator column past the closing `)`

Source: Q25 clarification (2026-05-20). REFINES the Q10 rule.

In a multi-line boolean condition for an `if` / `while` / `for`,
the trailing operators (`&&`, `||`) must be column-aligned. The
Q25 clarification adds that the alignment column must be **to
the RIGHT of the column where the closing `)` of the condition
lands** on the last line.

```c
if (cond_one   &&             /* && at, say, col 16             */
    cond_two   &&             /* same col 16                    */
    cond_three)               /* `)` at col 13 -- operators are */
{                             /* past it                        */
   ...
}
```

If the natural end-of-expression position for the operator
would put it AT OR BEFORE the closing-paren column, padding
spaces are added on the operator lines so the operators reach
the required column.

This is a refinement of the "operators column-aligned" rule
from Q10's footnote. The alignment column is not free -- it
must be past the `)`.

**Break-direction rule (hard).** Always break AFTER the
operator (operator at end of previous line); never break
BEFORE the operator. The "operator at start of continuation"
form is forbidden.

**Linter implication:**
  - Detect multi-line conditions of `if` / `while` / `for`.
  - Find the trailing operators on the wrapped lines.
  - Verify they're all in the same column.
  - Verify that column > the closing `)` column on the last
    line.
  - Also verify the break is after the operator (not before).

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

### Extraction justifications (Q10, Q32, Q48)

Three distinct justifications for extracting to a local, all
of which must be examined when considering an extraction:

  1. **Reuse (Q32):** the extracted sub-piece appears in
     multiple places. Extract enables sharing.

  2. **Dedup (Q10):** a single local replaces multiple
     references to the same expression within one function.
     Cost 1 (local), savings = number of references.

  3. **Simplification via decomposition (Q48):** the
     extracted sub-expressions are simpler / better-named
     than the inline original. Even with a single use, the
     decomposition pays for itself in readability.

An extraction that satisfies NONE of these three is
anti-pattern -- Q48 V2-style ("very bad"): relocating an
expression to a local without simplifying it, just adding a
line and a name for no benefit.

Linter implication: when suggesting `extract-to-local`,
check that the suggestion satisfies at least one of the
three. The simplification check is the fuzziest -- proxy
metrics include operator count, nesting depth, distinct
identifier count.

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
