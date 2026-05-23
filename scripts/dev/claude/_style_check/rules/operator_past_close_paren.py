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

# Q25 refinement: in a multi-line condition, the trailing operator
# column must be PAST the column of the closing `)` of the
# condition on the last line. This is on top of `align_multiline_
# operators` (which says the operator columns must agree).
#
# Detection: identify multi-line `if (...)` / `while (...)` /
# `for (...)` whose `(` and `)` are on different lines. The trailing
# operators on the wrapped lines should sit at a column > the close-
# paren column on the final line.

_COND_OPEN = re.compile(r'\b(if|while|for)\s*\(')
_OP_END = re.compile(r'(&&|\|\|)\s*$')


class OperatorPastClosePAren(Rule):  # pylint: disable=invalid-name

   id = 'operator_past_close_paren'
   description = (
      'In a multi-line `if`/`while`/`for` condition, trailing '
      '`&&` / `||` operators on wrapped lines must sit in a column '
      'to the RIGHT of where the final closing `)` lands -- pad '
      'with spaces if needed (Q25 refinement).'
   )
   layers = LAYER_TOKENS
   severity = SEVERITY_WARNING
   default_score = SCORE_SOFT

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      masked = _tokens_mod.mask_non_code(ctx.source_text)
      out = []

      pos = 0

      while True:

         m = _COND_OPEN.search(masked, pos)

         if not m:
            break

         open_paren = m.end() - 1
         close = _tokens_mod.find_matching_close(masked, open_paren)

         if close < 0:
            pos = m.end()
            continue

         open_line, _ = _tokens_mod.offset_to_line_col(masked, open_paren)
         close_line, close_col = _tokens_mod.offset_to_line_col(masked, close)

         pos = close + 1

         if close_line == open_line:
            continue   # single-line condition

         # Walk the lines [open_line, close_line - 1] and find
         # trailing && / || operators.
         lines = masked.split('\n')

         for ln_idx in range(open_line - 1, close_line - 1):

            if ln_idx >= len(lines):
               continue

            l = lines[ln_idx]
            mm = _OP_END.search(l)

            if not mm:
               continue

            op_col = l.rfind(mm.group(1)) + 1

            if op_col > close_col:
               continue   # operator already past `)` column

            line_text = ctx.lines[ln_idx] \
               if ln_idx < len(ctx.lines) else ''

            out.append(Diagnostic(
               file=str(ctx.file_path),
               line=ln_idx + 1,
               col=op_col,
               end_line=ln_idx + 1,
               end_col=op_col + 2,
               rule=self.id,
               severity=self.severity,
               message=(
                  'operator `{}` at column {}; the condition\'s '
                  'closing `)` is at column {} on line {} -- pad '
                  'with spaces so the operator column is to the '
                  'right of `)`'
               ).format(mm.group(1), op_col, close_col, close_line),
               snippet=line_text,
            ))

      return out


RULE = OperatorPastClosePAren()
