# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring

from typing import List

from .base import (
   Rule,
   Diagnostic,
   CheckContext,
   LAYER_RAW_TEXT,
)

MAX_COLS = 80


class Cols80(Rule):

   id = 'cols_80'
   description = (
      "Lines must not exceed 80 columns (strict, no exceptions per "
      "docs/contributing.md NOTE[1])"
   )
   layers = LAYER_RAW_TEXT

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      out = []

      for i, line in enumerate(ctx.lines, start=1):

         # Trailing whitespace doesn't count toward column width but is
         # caught by a separate rule (trailing_ws) in M2.
         stripped = line.rstrip()
         length = len(stripped)

         if length <= MAX_COLS:
            continue

         over = length - MAX_COLS

         if over <= 3:
            hint = (
               'over by {} col(s) -- try: compact `, ` to `,`, '
               'remove spaces around +/- in pointer math, '
               'shorten a trailing comment, move {{ to own line, '
               'reduce column padding in the block'
            ).format(over)
         else:
            hint = (
               'over by {} cols -- consider wrapping the line '
               'or extracting a local variable'
            ).format(over)

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=i,
            col=MAX_COLS + 1,
            end_line=i,
            end_col=length + 1,
            rule=self.id,
            severity=self.severity,
            message='line is {} cols (max {})'.format(length, MAX_COLS),
            snippet=stripped[:MAX_COLS] + ' ...',
            suggestion=hint,
         ))

      return out


RULE = Cols80()
