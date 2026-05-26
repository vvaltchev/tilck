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
   SCORE_STRONG_PREF,
)
from .. import tokens as _tokens_mod

# Q25 hard rule: multi-line conditions break AFTER the operator,
# never BEFORE. A continuation line that starts (after indentation)
# with `&&` or `||` is the forbidden break-before form.
_PAT = re.compile(r'^[ \t]+(&&|\|\|)(?=\s)')


class BreakBeforeOperatorForbidden(Rule):

   id = 'break_before_operator_forbidden'
   description = (
      'Multi-line boolean conditions break AFTER the operator '
      '(operator at end of previous line), never BEFORE (operator '
      'at start of continuation line). Q25 hard rule.'
   )
   layers = LAYER_TOKENS
   severity = SEVERITY_WARNING
   default_score = SCORE_STRONG_PREF

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      masked = _tokens_mod.mask_non_code(ctx.source_text)
      out = []

      raw_lines = ctx.lines

      for i, line in enumerate(masked.split('\n'), start=1):

         m = _PAT.match(line)

         if not m:
            continue

         # Skip preprocessor continuation lines: `#if` / `#elif`
         # conditions often wrap with `|| defined(...)` at the start
         # of the next line, and that's a different convention.
         prev_raw = raw_lines[i - 2].rstrip() if i >= 2 else ''

         if prev_raw.endswith('\\'):
            continue

         op = m.group(1)
         col = m.start(1) + 1

         line_text = raw_lines[i - 1] if i - 1 < len(raw_lines) else ''

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=i,
            col=col,
            end_line=i,
            end_col=col + len(op),
            rule=self.id,
            severity=self.severity,
            message=('operator `{}` starts a continuation line; the '
                     'break must be AFTER the operator (operator at '
                     'end of previous line)').format(op),
            snippet=line_text,
         ))

      return out


RULE = BreakBeforeOperatorForbidden()
