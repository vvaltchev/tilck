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
   LAYER_RAW_TEXT,
   SEVERITY_WARNING,
   SCORE_MEDIUM_PREF,
)


class DefineBackslashAlign(Rule):

   id = 'define_backslash_align'
   description = (
      'Multi-line #define: all backslash continuations must be '
      'at the same column within a single macro definition.'
   )
   layers = LAYER_RAW_TEXT
   severity = SEVERITY_WARNING
   default_score = SCORE_MEDIUM_PREF

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      out = []
      lines = ctx.lines
      i = 0

      while i < len(lines):

         line = lines[i]

         if not line.lstrip().startswith('#'):
            i += 1
            continue

         stripped = line.rstrip()

         if not stripped.endswith('\\'):
            i += 1
            continue

         macro_start = i
         macro_lines = []

         while i < len(lines):

            stripped = lines[i].rstrip()

            if stripped.endswith('\\'):
               macro_lines.append((i, stripped))
               i += 1
            else:
               break

         if len(macro_lines) < 2:
            continue

         cols = []

         for ln_idx, ln_text in macro_lines:
            bs_col = len(ln_text)
            cols.append((ln_idx, bs_col))

         target = max(c for _, c in cols)

         for ln_idx, bs_col in cols:

            if bs_col == target:
               continue

            line_no = ln_idx + 1
            line_text = lines[ln_idx]
            content = line_text.rstrip().rstrip('\\').rstrip()
            pad = target - len(content) - 1
            fixed = content + ' ' * pad + ' \\'

            out.append(Diagnostic(
               file=str(ctx.file_path),
               line=line_no,
               col=bs_col,
               end_line=line_no,
               end_col=bs_col + 1,
               rule=self.id,
               severity=self.severity,
               message=(
                  'backslash at column {}; other continuations '
                  'in this macro are at column {} -- align'
               ).format(bs_col, target),
               snippet=line_text.rstrip(),
               fixes=[Fix(line_no, line_no, [fixed])],
            ))

      return out


RULE = DefineBackslashAlign()
