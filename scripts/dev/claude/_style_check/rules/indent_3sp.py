# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=consider-using-f-string

from typing import List

from .base import (
   Rule,
   Diagnostic,
   Fix,
   CheckContext,
   LAYER_RAW_TEXT,
)


class Indent3sp(Rule):

   id = 'indent_3sp'
   description = '3-space indentation; never tabs in leading whitespace'
   layers = LAYER_RAW_TEXT

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      out = []

      for i, line in enumerate(ctx.lines, start=1):

         stripped = line.lstrip(' \t')
         leading = line[:len(line) - len(stripped)]

         if '\t' not in leading:
            continue

         fixed_leading = leading.replace('\t', '   ')
         fixed_line = fixed_leading + stripped

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=i,
            col=1,
            end_line=i,
            end_col=len(leading) + 1,
            rule=self.id,
            severity=self.severity,
            message='leading whitespace contains tab(s); use 3 spaces',
            snippet=line.expandtabs(8),
            fixes=[Fix(i, i, [fixed_line],
                        'convert leading tabs to 3 spaces')],
         ))

      return out


RULE = Indent3sp()
