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


# Preferred backslash column (1-based line length including the \).
# The corpus overwhelmingly uses column 71; the doc says "column 80"
# but in practice that means 80-col lines with the \ at position 71
# (content up to ~69, space, \).  When any content line exceeds
# PREFERRED_COL - 2 chars, fall back to body-aligned (longest
# content + 2).
PREFERRED_COL = 71


class DefineBackslashAlign(Rule):

   id = 'define_backslash_align'
   description = (
      'Multi-line #define: all backslash continuations must be '
      'at the same column.  Preferred column is {} (matching the '
      'corpus convention); when content is too long, fall back to '
      'body-aligned (longest line + 2).'
   ).format(PREFERRED_COL)
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

         entries = []

         for ln_idx, ln_text in macro_lines:
            content = ln_text.rstrip('\\').rstrip()
            bs_col = len(ln_text)
            entries.append((ln_idx, content, bs_col))

         max_content = max(len(c) for _, c, _ in entries)
         body_target = max_content + 2

         if body_target <= PREFERRED_COL:
            target = PREFERRED_COL
         else:
            target = body_target

         if all(bs_col == target for _, _, bs_col in entries):
            continue

         first_ln = entries[0][0] + 1
         last_ln = entries[-1][0] + 1

         new_lines = []

         for _, content, _ in entries:
            pad = target - len(content) - 2
            new_lines.append(content + ' ' * max(pad, 0) + ' \\')

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=first_ln,
            col=1,
            end_line=last_ln,
            end_col=1,
            rule=self.id,
            severity=self.severity,
            message=(
               'backslash continuations in this macro are not '
               'column-aligned (lines {}-{}) -- align all to '
               'column {}'
            ).format(first_ln, last_ln, target),
            snippet=lines[entries[0][0]].rstrip(),
            fixes=[Fix(first_ln, last_ln, new_lines)],
         ))

      return out


RULE = DefineBackslashAlign()
