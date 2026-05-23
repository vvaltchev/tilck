# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=consider-using-f-string

import re

from typing import List

from .base import (
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
# Single-char & or | that isn't doubled (&&/||) or part of `|=` / `&=`.
_BITWISE = re.compile(r'(?<![&|])([&|])(?![&|=])')


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

         if not _BITWISE.search(line):
            continue

         # Light gate: if the line is already "parenthesized enough",
         # don't fire. Counting parens isn't perfect but it filters
         # cases where the user already wrote `((a << b) & c)`.
         opens = line.count('(')
         closes = line.count(')')

         # If there are at least 2 paren pairs on the line, assume
         # the author already structured it.
         if opens >= 2 and closes >= 2:
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
         ))

      return out


RULE = ParenExplicitPrecedence()
