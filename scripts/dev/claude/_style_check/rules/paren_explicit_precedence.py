# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=consider-using-f-string

import re

from typing import List

from .base import (
   COST_MILD,
   Rule,
   Diagnostic,
   CheckContext,
   LAYER_TOKENS,
   SEVERITY_WARNING,
   SCORE_SOFT,
)
from .. import tokens as _tokens_mod

# Q37: prefer explicit parens for bit-twiddle expressions that mix
# operators of low-recall precedence (shift vs bitwise-and-or).
# Detect lines that contain BOTH a shift (`<<` / `>>`) and a
# bitwise-and/or (`&` / `|`) WITHOUT being fully parenthesized.
# Heuristic only; the rule is SOFT.
#
# The detection is intentionally conservative: we flag only when
# both ops appear in the same statement AND there are no parens
# around either sub-expression on that line.

_SHIFT = re.compile(r'(<<|>>)')
# Single-char & or | that isn't doubled (&&/||) or part of `|=`/`&=`.
# Also exclude unary address-of `&`: preceded by `=`, `(`, `,`, or
# start-of-expression contexts.
_BITWISE = re.compile(r'(?<![&|])([&|])(?![&|=])')
_ADDR_OF = re.compile(r'[=,(]\s*&\w')


class ParenExplicitPrecedence(Rule):

   id = 'paren_explicit_precedence'
   description = (
      'Bit-twiddle expressions mixing shifts (`<<` / `>>`) and '
      'bitwise ops (`&` / `|`) should parenthesize each sub-'
      'expression -- C precedence between these ops is easy to '
      'misremember (Q37 soft preference).'
   )
   layers = LAYER_TOKENS
   severity = SEVERITY_WARNING
   default_score = SCORE_SOFT

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      masked = _tokens_mod.mask_non_code(ctx.source_text)
      out = []

      for i, line in enumerate(masked.split('\n'), start=1):

         if not _SHIFT.search(line):
            continue

         # Filter out unary address-of `&` before checking for
         # bitwise ops: mask `= &var` / `(&var` / `, &var` so they
         # don't look like bitwise-AND.
         line_no_addrof = _ADDR_OF.sub(lambda m: ' ' * len(m.group()), line)

         if not _BITWISE.search(line_no_addrof):
            continue

         # Check whether each shift and each bitwise operator is
         # already inside a parenthesized sub-expression. If every
         # shift is at depth >= 1, or every bitwise op is at depth
         # >= 1, the expression is already disambiguated — parens
         # around EITHER side of the precedence boundary suffice.
         def _all_at_depth(pattern, text):

            for sm in pattern.finditer(text):
               depth = 0
               for k in range(sm.start()):
                  if text[k] == '(':
                     depth += 1
                  elif text[k] == ')':
                     depth -= 1
               if depth < 1:
                  return False

            return True

         if _all_at_depth(_SHIFT, line):
            continue

         if _all_at_depth(_BITWISE, line_no_addrof):
            continue

         # Find a column to report at -- use the first shift.
         m = _SHIFT.search(line)
         col = m.start() + 1

         line_text = ctx.lines[i - 1] \
            if i - 1 < len(ctx.lines) else ''

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=i,
            col=col,
            end_line=i,
            end_col=col + 2,
            rule=self.id,
            severity=self.severity,
            message=(
               'expression mixes shift and bitwise operators without '
               'sub-expression parens -- precedence is fragile, '
               'parenthesize each sub-expression'
            ),
            snippet=line_text.strip(),
            is_gradient=True,
            prettiness_cost=COST_MILD,
         ))

      return out


RULE = ParenExplicitPrecedence()
