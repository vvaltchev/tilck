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

(none yet)
