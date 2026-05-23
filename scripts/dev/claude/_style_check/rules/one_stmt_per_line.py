# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring

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

# Two top-level statements on one line: `<expr>; [whitespace] [a-zA-Z_]...`.
# We run this on a masked source where comments / strings are blanked
# out, and we track paren depth so that `for (i = 0; i < n; i++)` does
# not trigger.

_STMT_HEAD_CLASS = set('abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_')


class OneStmtPerLine(Rule):

   id = 'one_stmt_per_line'
   description = 'Do not pack multiple statements on one line'
   layers = LAYER_TOKENS

   severity = SEVERITY_WARNING

   default_score = SCORE_STRONG_PREF


   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      masked = _tokens_mod.mask_non_code(ctx.source_text)
      out = []
      seen = set()

      paren_depth = 0
      brace_depth = 0
      n = len(masked)
      i = 0

      # State per line: position of the most recent top-level `;` (i.e.,
      # paren_depth == 0) AND its brace_depth. We only flag the pair
      # when both semicolons are at the SAME brace_depth -- that's what
      # makes them statement-level peers (and rules out `typedef struct
      # { x; } name;` which has two `;` at different depths).
      last_semi_pos = -1
      last_semi_brace_depth = -1

      while i < n:

         c = masked[i]

         if c == '(':
            paren_depth += 1
         elif c == ')':
            paren_depth = max(0, paren_depth - 1)
         elif c == '{':
            brace_depth += 1
         elif c == '}':
            brace_depth = max(0, brace_depth - 1)

         if c == '\n':
            last_semi_pos = -1
            last_semi_brace_depth = -1
            i += 1
            continue

         if c == ';' and paren_depth == 0:

            if (last_semi_pos >= 0
                and last_semi_brace_depth == brace_depth):

               between = masked[last_semi_pos + 1:i]

               if any(ch in _STMT_HEAD_CLASS for ch in between):

                  line, col = _tokens_mod.offset_to_line_col(
                     masked, last_semi_pos + 1
                  )

                  key = (line,)

                  if key not in seen:

                     seen.add(key)
                     line_text = ctx.lines[line - 1] \
                        if line - 1 < len(ctx.lines) else ''

                     out.append(Diagnostic(
                        file=str(ctx.file_path),
                        line=line,
                        col=col,
                        end_line=line,
                        end_col=col + len(between),
                        rule=self.id,
                        severity=self.severity,
                        message='one statement per line; do not pack',
                        snippet=line_text.strip(),
                     ))

            last_semi_pos = i
            last_semi_brace_depth = brace_depth

         i += 1

      return out


RULE = OneStmtPerLine()
