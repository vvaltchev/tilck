# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=consider-using-f-string

from typing import List

from .base import (
   Rule,
   Diagnostic,
   CheckContext,
   LAYER_RAW_TEXT,
)


class TrailingWs(Rule):

   id = 'trailing_ws'
   description = 'No trailing whitespace at end of line'
   layers = LAYER_RAW_TEXT

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      out = []

      for i, line in enumerate(ctx.lines, start=1):

         stripped = line.rstrip(' \t')

         if len(stripped) == len(line):
            continue

         # Blank lines that are pure whitespace -- still a violation.
         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=i,
            col=len(stripped) + 1,
            end_line=i,
            end_col=len(line) + 1,
            rule=self.id,
            severity=self.severity,
            message='{} char(s) of trailing whitespace'.format(
               len(line) - len(stripped)
            ),
            snippet=line[:80] if line else '',
         ))

      return out


RULE = TrailingWs()
