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
   description = ('Space-only indentation (width from --indent, default '
                  '3); never tabs in leading whitespace')
   layers = LAYER_RAW_TEXT

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      out = []
      spaces = ' ' * ctx.indent

      for i, line in enumerate(ctx.lines, start=1):

         stripped = line.lstrip(' \t')
         leading = line[:len(line) - len(stripped)]

         if '\t' not in leading:
            continue

         fixed_leading = leading.replace('\t', spaces)
         fixed_line = fixed_leading + stripped

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=i,
            col=1,
            end_line=i,
            end_col=len(leading) + 1,
            rule=self.id,
            severity=self.severity,
            message=('leading whitespace contains tab(s); use {} '
                     'spaces').format(ctx.indent),
            snippet=line.expandtabs(8),
            fixes=[Fix(i, i, [fixed_line],
                        'convert leading tabs to {} spaces'.format(
                           ctx.indent))],
         ))

      return out


RULE = Indent3sp()
