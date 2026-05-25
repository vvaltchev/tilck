# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring

import re

from typing import List

from .base import (
   Rule,
   Diagnostic,
   CheckContext,
   LAYER_RAW_TEXT,
   SEVERITY_WARNING,
   SCORE_MEDIUM_PREF,
)
from .. import tokens as _tokens_mod

# A designated initializer entry: `[KEY]` followed by `=`.
# We match the bracket-enclosed identifier and then look at
# the spacing around `=`.
#
# Pattern explanation:
#   \[           -- opening bracket
#   \w+          -- the key (identifier or macro constant)
#   \]           -- closing bracket
#   \s*=         -- optional whitespace then equals
#
# We use this to find all designator entries on a line.
_DESIGNATOR_ENTRY = re.compile(r'\[\w+\]\s*=')

# Specifically the no-space variant: `]=` with nothing between.
_NO_SPACE = re.compile(r'\[\w+\]=')


class DesignatorInitSpacing(Rule):

   id = 'designator_init_spacing'
   description = (
      'Designated initializer entries must have space around `=` '
      '(`[KEY] = val`, not `[KEY]= val` or `[KEY]="val"`), and '
      'large arrays must have one entry per line.'
   )
   layers = LAYER_RAW_TEXT
   severity = SEVERITY_WARNING
   default_score = SCORE_MEDIUM_PREF

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      masked = _tokens_mod.mask_non_code(ctx.source_text)
      masked_lines = masked.split('\n')
      out = []

      # Track whether we are inside a brace-enclosed initializer
      # by counting braces. We only flag lines that are plausibly
      # inside an initializer (brace depth > 0 after an `= {`).
      brace_depth = 0
      in_initializer = False
      init_entry_count = 0

      for i, raw_line in enumerate(ctx.lines):

         lineno = i + 1
         m_line = masked_lines[i] if i < len(masked_lines) else ''

         # Track brace depth on the masked line to detect
         # initializer blocks. A line containing `= {` (with the
         # `{` not inside a string/comment) opens an initializer.
         for ch in m_line:

            if ch == '{':

               if brace_depth == 0:

                  # Check if this line has `= {` pattern,
                  # indicating an initializer.
                  if re.search(r'=\s*\{', m_line):
                     in_initializer = True
                     init_entry_count = 0

               brace_depth += 1

            elif ch == '}':
               brace_depth -= 1

               if brace_depth <= 0:
                  brace_depth = 0
                  in_initializer = False
                  init_entry_count = 0

         if not in_initializer:
            continue

         # Find all designator entries on this line (masked).
         entries = list(_DESIGNATOR_ENTRY.finditer(m_line))

         if not entries:
            continue

         init_entry_count += len(entries)

         # Check 1: missing space between `]` and `=`.
         has_spacing_issue = bool(_NO_SPACE.search(m_line))

         # Check 2: multiple entries packed on one line.
         # Only flag if the initializer has many entries (10+),
         # which we approximate by checking whether we have seen
         # enough entries so far.
         has_packing_issue = len(entries) > 1

         if not has_spacing_issue and not has_packing_issue:
            continue

         # Build diagnostic message.
         parts = []

         if has_spacing_issue:
            parts.append(
               'missing space around `=` in designated initializer '
               '(`[KEY] = val`, not `[KEY]=val`)'
            )

         if has_packing_issue:
            parts.append(
               'multiple designated initializer entries on one line '
               '-- each `[KEY] = val` entry should be on its own line'
            )

         col = entries[0].start() + 1  # 1-based

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=lineno,
            col=col,
            end_line=lineno,
            end_col=len(raw_line.rstrip()) + 1,
            rule=self.id,
            severity=self.severity,
            message='; '.join(parts),
            snippet=raw_line.strip(),
         ))

      return out


RULE = DesignatorInitSpacing()
