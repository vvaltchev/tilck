#  SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=consider-using-f-string

from typing import List

from .base import (
   Rule,
   Diagnostic,
   CheckContext,
   LAYER_TOKENS,
   SEVERITY_WARNING,
   SCORE_NUDGE,
   STATEMENT_HARD_FAIL_THRESHOLD,
)
from .. import tokens as _tokens_mod

# Gradient rule with SUPER-LINEAR per-statement growth.
#
# The first `::` in a statement is free -- one qualifier is perfectly
# reasonable in C++ (no other way to disambiguate `std::vector` etc.).
# Beyond that, every additional `::` in the same statement raises the
# cumulative cost quadratically. The 4th qualifier in a single
# statement triggers a hard failure on its own; the rule encodes that
# the user can always flatten with `using namespace`, `using`
# declarations, namespace aliases, or type aliases.
#
# Cumulative cost(N) = _BASE * (N - 1) ** _EXPONENT, with N=1 -> 0:
#
#    N=1 -> 0.00     N=2 -> 0.40     N=3 -> 1.60
#    N=4 -> 3.60 (HARD FAILURE)      N=5 -> 6.40     N=6 -> 10.00

_BASE = 0.40
_EXPONENT = 2.0


def _cumulative_cost(count: int) -> float:
   if count < 2:
      return 0.0
   return _BASE * ((count - 1) ** _EXPONENT)


class QualifiedNameDensity(Rule):

   id = 'qualified_name_density'
   description = (
      'C++ statements that chain multiple :: qualifiers accumulate '
      'visual noise super-linearly. One :: is free; each additional '
      'one raises the cumulative cost quadratically. Four ::s in one '
      'statement is a hard failure. Flatten with `using namespace`, '
      '`using`-declarations, namespace aliases (`namespace x = ...;`'
      '), or type aliases. (Gradient rule -- not a violation, but '
      'can escalate to a hard failure when one statement is severely '
      'over-qualified.)'
   )
   layers = LAYER_TOKENS
   severity = SEVERITY_WARNING
   default_score = SCORE_NUDGE
   applies_to = {'.cpp', '.hpp'}

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      if not ctx.is_cpp:
         return []

      masked = _tokens_mod.mask_non_code(ctx.source_text)
      out = []

      # Walk the masked source. Split into statements at every `;`
      # that sits at paren-depth 0 (so `for (a; b; c)` is one
      # statement; its inner `;`s are inside the for-header parens,
      # not at depth 0).
      n = len(masked)
      stmt_start = 0
      paren_depth = 0
      i = 0

      while i < n:

         c = masked[i]

         if c in '([':
            paren_depth += 1
         elif c in ')]':

            if paren_depth > 0:
               paren_depth -= 1

         elif c == ';' and paren_depth == 0:

            self._maybe_emit(out, ctx, masked, stmt_start, i)
            stmt_start = i + 1

         i += 1

      # Trailing chunk after the last `;` -- typically nothing, but
      # cover top-of-file declarations without a trailing newline.
      self._maybe_emit(out, ctx, masked, stmt_start, n)

      return out

   def _maybe_emit(self, out, ctx, masked, start, end):

      stmt_text = masked[start:end]
      count = stmt_text.count('::')

      if count < 2:
         return

      cost = _cumulative_cost(count)
      hard_fail = cost >= STATEMENT_HARD_FAIL_THRESHOLD

      # Anchor the diagnostic at the first `::` in the statement.
      first = masked.find('::', start, end)

      if first < 0:
         return

      line, col = _tokens_mod.offset_to_line_col(masked, first)
      line_text = ctx.lines[line - 1] \
         if line - 1 < len(ctx.lines) else ''

      tag = ' (HARD FAILURE)' if hard_fail else ''

      out.append(Diagnostic(
         file=str(ctx.file_path),
         line=line,
         col=col,
         end_line=line,
         end_col=col + 2,
         rule=self.id,
         severity=self.severity,
         message=(
            '{} `::` qualifiers in one statement{} -- flatten with '
            '`using namespace`, a using-declaration, or an alias '
            '(cumulative cost {:.2f})'.format(count, tag, cost)
         ),
         snippet=line_text.strip(),
         is_gradient=True,
         prettiness_cost=cost,
      ))


RULE = QualifiedNameDensity()
