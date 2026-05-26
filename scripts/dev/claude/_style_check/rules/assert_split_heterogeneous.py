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

# Q43: multi-clause heterogeneous ASSERT loses diagnostic
# specificity on failure. Split into separate ASSERTs so the
# triggering clause is identifiable.
#
# Detection: a single ASSERT() spanning multiple lines with `&&`
# operators connecting heterogeneous-looking clauses. This rule
# is intentionally narrow: we fire only when the ASSERT body has
# 2+ `&&` operators (i.e., 3+ clauses) -- a 2-clause ASSERT might
# be a legitimate homogeneous range check.

_ASSERT_OPEN = re.compile(r'\bASSERT\s*\(')

_CMP_PAT = re.compile(
   r'^\s*(\w[\w\[\].>-]*)\s*(<=?|>=?)\s*(\w[\w\[\].>-]*)\s*$'
)


def _is_chained_ordering(body):
   """Return True if every &&-clause is a simple comparison and
   adjacent clauses share an operand (chained range check like
   a <= b && b < c && c <= d)."""

   clauses = [c.strip() for c in body.split('&&')]
   prev_rhs = None

   for clause in clauses:

      m = _CMP_PAT.match(clause)

      if not m:
         return False

      lhs, op, rhs = m.group(1), m.group(2), m.group(3)

      if op in ('<', '<='):

         if prev_rhs is not None and lhs != prev_rhs:
            return False

         prev_rhs = rhs

      elif op in ('>', '>='):

         if prev_rhs is not None and rhs != prev_rhs:
            return False

         prev_rhs = lhs

   return True


class AssertSplitHeterogeneous(Rule):

   id = 'assert_split_heterogeneous'
   description = (
      'Multi-clause `ASSERT(a && b && c)` loses diagnostic '
      'specificity on failure -- you can\'t tell WHICH clause '
      'fired. Split into separate `ASSERT(a); ASSERT(b); '
      'ASSERT(c);` so the failure points at the broken invariant. '
      '(Q43; the rule fires for 3+ conjoined clauses.)'
   )
   layers = LAYER_TOKENS
   severity = SEVERITY_WARNING
   default_score = SCORE_SOFT

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      masked = _tokens_mod.mask_non_code(ctx.source_text)
      out = []

      pos = 0

      while True:

         m = _ASSERT_OPEN.search(masked, pos)

         if not m:
            break

         open_paren = m.end() - 1
         close = _tokens_mod.find_matching_close(masked, open_paren)

         if close < 0:
            pos = m.end()
            continue

         body = masked[open_paren + 1:close]
         and_count = body.count('&&')

         pos = close + 1

         if and_count < 2:
            continue

         if '||' in body:
            continue

         if _is_chained_ordering(body):
            continue

         line, col = _tokens_mod.offset_to_line_col(masked, m.start())
         line_text = ctx.lines[line - 1] \
            if line - 1 < len(ctx.lines) else ''

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=line,
            col=col,
            end_line=line,
            end_col=col + 6,
            rule=self.id,
            severity=self.severity,
            message=(
               'ASSERT with {} `&&`-conjoined clauses -- on failure '
               'you cannot tell which clause fired; split into one '
               'ASSERT per clause'
            ).format(and_count + 1),
            snippet=line_text.strip(),
         ))

      return out


RULE = AssertSplitHeterogeneous()
