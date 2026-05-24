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
   LAYER_RAW_TEXT,
   SEVERITY_WARNING,
   SCORE_NUDGE,
)
from .. import tokens as _tokens_mod

# Detect the pattern:
#
#   if (cond)
#      return X;
#   else
#      return Y;
#
# Preferred form (guard clause):
#
#   if (cond)
#      return X;
#
#   return Y;
#
# The `else` is redundant when the if-branch unconditionally returns.

_RETURN_LINE = re.compile(r'^\s*return\b[^;{]*;\s*$')
_ELSE_LINE = re.compile(r'^\s*\}\s*else\s*$|^\s*else\s*$')


class ElseAfterReturn(Rule):

   id = 'else_after_return'
   description = (
      'Prefer guard-clause style: when an `if` branch ends with '
      '`return`, drop the `else` and dedent the alternative. The '
      '`else` is redundant after an unconditional return. Very soft '
      'preference (nudge).'
   )
   layers = LAYER_RAW_TEXT
   severity = SEVERITY_WARNING
   default_score = SCORE_NUDGE

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      masked = _tokens_mod.mask_non_code(ctx.source_text)
      masked_lines = masked.split('\n')
      out = []

      for i, line in enumerate(masked_lines):

         if not _RETURN_LINE.match(line):
            continue

         if i + 1 >= len(masked_lines):
            continue

         next_line = masked_lines[i + 1]

         if not _ELSE_LINE.match(next_line):
            continue

         line_no = i + 2   # 1-based line of the `else`
         line_text = ctx.lines[i + 1] if i + 1 < len(ctx.lines) else ''

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=line_no,
            col=1,
            end_line=line_no,
            end_col=len(line_text) + 1,
            rule=self.id,
            severity=self.severity,
            message=(
               '`else` after unconditional `return` is redundant -- '
               'prefer guard-clause style: drop `else`, add blank '
               'line, dedent the alternative'
            ),
            snippet=line_text.strip(),
            suggestion='remove `else`, add blank line after return',
         ))

      return out


RULE = ElseAfterReturn()
