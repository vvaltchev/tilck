# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=consider-using-f-string

import re

from typing import List

from .base import (
   Rule,
   Diagnostic,
   Fix,
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
_OP_END = re.compile(r'(&&|\|\||\||\&)\s*$')


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

         lines = masked.split('\n')
         target = close_col + 1

         # Collect all trailing operators in this condition.
         ops = []

         for ln_idx in range(open_line - 1, close_line - 1):

            if ln_idx >= len(lines):
               continue

            l = lines[ln_idx]
            mm = _OP_END.search(l)

            if not mm:
               continue

            op_col = l.rfind(mm.group(1)) + 1
            ops.append((ln_idx, op_col, mm.group(1)))

         # Check for under-padded operators.
         for ln_idx, op_col, op in ops:

            if op_col > close_col:
               continue

            line_text = ctx.lines[ln_idx] \
               if ln_idx < len(ctx.lines) else ''

            pad = target - op_col
            op_pos = line_text.rfind(op)
            fixed = line_text[:op_pos] + ' ' * pad + \
               line_text[op_pos:]

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
               ).format(op, op_col, close_col, close_line),
               snippet=line_text,
               fixes=[Fix(ln_idx + 1, ln_idx + 1, [fixed])],
            ))

         # Check for over-padded operators, but only when there
         # is a single operator.  When multiple operators align
         # to the longest expression, the column is set by
         # align_multiline_operators and shouldn't be questioned.
         if len(ops) != 1:
            continue

         ln_idx, op_col, op = ops[0]

         if op_col <= target + 2:
            continue

         line_text = ctx.lines[ln_idx] \
            if ln_idx < len(ctx.lines) else ''
         op_pos = line_text.rfind(op)
         pre = line_text[:op_pos]
         gap = len(pre) - len(pre.rstrip())

         if gap <= 1:
            continue

         trim = op_col - target
         fixed = line_text[:op_pos - trim] + line_text[op_pos:]

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=ln_idx + 1,
            col=op_col,
            end_line=ln_idx + 1,
            end_col=op_col + 2,
            rule=self.id,
            severity=self.severity,
            message=(
               'operator `{}` at column {} is over-padded; '
               'the condition\'s closing `)` is at column {} '
               'on line {} -- place the operator close to '
               'the `)`, not far past it'
            ).format(op, op_col, close_col, close_line),
            snippet=line_text,
            fixes=[Fix(ln_idx + 1, ln_idx + 1, [fixed])],
         ))

      return out


RULE = OperatorPastClosePAren()
