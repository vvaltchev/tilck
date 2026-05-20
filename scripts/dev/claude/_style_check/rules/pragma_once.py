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
)

# Files exempt from the rule (upstream/external headers preserve their
# original style):
EXEMPT_PATH_FRAGMENTS = (
   '/include/system_headers/',
   '/3rd_party/',
)

# Look for #pragma once and ifndef-style guards within the first ~20
# lines of a header. The user's headers consistently put #pragma once
# on line 3 (after SPDX + blank); v1 just requires its presence
# somewhere near the top, and the absence of the legacy guard pattern.
HEAD_SCAN_LINES = 20

_PAT_IFNDEF = re.compile(r'^\s*#\s*ifndef\s+([A-Z_][A-Z0-9_]*_H_*)\s*$')
_PAT_DEFINE = re.compile(r'^\s*#\s*define\s+([A-Z_][A-Z0-9_]*_H_*)\b')


class PragmaOnce(Rule):

   id = 'pragma_once'
   description = (
      'Headers must use "#pragma once" (not "#ifndef _X_H_" guards)'
   )
   layers = LAYER_RAW_TEXT
   applies_to = {'.h'}

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      path_s = str(ctx.file_path)

      if any(frag in path_s for frag in EXEMPT_PATH_FRAGMENTS):
         return []

      out = []

      has_pragma = False
      guard_ifndef_line = None
      guard_sym = None

      head = ctx.lines[:HEAD_SCAN_LINES]

      for i, line in enumerate(head, start=1):

         if line.strip() == '#pragma once':
            has_pragma = True

         if guard_ifndef_line is None:

            m = _PAT_IFNDEF.match(line)

            if m:
               guard_sym = m.group(1)
               guard_ifndef_line = i

      # Verify the ifndef is part of a real guard: check that a #define
      # of the same symbol follows shortly after.
      paired_guard = None

      if guard_ifndef_line is not None and guard_sym is not None:

         for j in range(guard_ifndef_line, min(guard_ifndef_line + 4,
                                               len(head))):

            cand = head[j]

            if cand.strip() == '':
               continue

            m2 = _PAT_DEFINE.match(cand)

            if m2 and m2.group(1) == guard_sym:
               paired_guard = guard_ifndef_line
               break

            break  # non-blank, non-matching line -- not a guard

      if paired_guard is not None:

         line_text = ctx.lines[paired_guard - 1]
         out.append(Diagnostic(
            file=path_s,
            line=paired_guard,
            col=1,
            end_line=paired_guard,
            end_col=len(line_text) + 1,
            rule=self.id,
            severity=self.severity,
            message=('use "#pragma once" instead of #ifndef {} header '
                     'guards').format(guard_sym),
            snippet=line_text.strip(),
         ))

      if not has_pragma and paired_guard is None:

         out.append(Diagnostic(
            file=path_s,
            line=1,
            col=1,
            end_line=1,
            end_col=1,
            rule=self.id,
            severity=self.severity,
            message='missing "#pragma once" near the top of the header',
            snippet=ctx.lines[0] if ctx.lines else '',
         ))

      return out


RULE = PragmaOnce()
