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
