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
   SEVERITY_WARNING,
   SCORE_MEDIUM_PREF,
)

# Q38 rule:
#   - For an `#ifdef` / `#if` / `#ifndef` block whose span (start line
#     to matching `#endif` line) is >= LONG_BLOCK_LINES, the `#endif`
#     MUST carry a trailing annotation (block comment `/* X */` or
#     line comment `// X`).
#   - Shorter blocks: annotation optional; no diagnostic.
#
# The matching is done by a simple stack walk of preprocessor directives;
# we don't need full PP expression parsing -- we just track nesting.

_IF_PAT     = re.compile(r'^\s*#\s*(if|ifdef|ifndef)\b\s*(.*)$')
_ENDIF_PAT  = re.compile(r'^\s*#\s*endif\b(.*)$')
_ANNOT_PAT  = re.compile(r'^\s*(?:/\*.*\*/|//.*)\s*$')

# Q38 ranking comment: "Long (~100+) | V2 or V3 mandatory". Choose
# 100 to match the user's stated threshold; smaller blocks
# (30..99 lines) are in the "annotation preferred but not flagged"
# zone.
LONG_BLOCK_LINES = 100


class EndifAnnotationLongBlocks(Rule):

   id = 'endif_annotation_long_blocks'
   description = (
      'Long `#endif` blocks (span >= {} lines) require a trailing '
      'annotation: `#endif /* COND */` (preferred) or `#endif '
      '// COND` (also acceptable). Shorter blocks: annotation '
      'optional. Q38 hard rule (length-based escalation).'
   ).format(LONG_BLOCK_LINES)
   layers = LAYER_RAW_TEXT
   severity = SEVERITY_WARNING
   default_score = SCORE_MEDIUM_PREF

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      stack = []     # list of (start_line, cond_text)
      out = []

      for i, line in enumerate(ctx.lines, start=1):

         m_if = _IF_PAT.match(line)

         if m_if:
            cond = m_if.group(2).strip()
            stack.append((i, cond))
            continue

         m_endif = _ENDIF_PAT.match(line)

         if not m_endif:
            continue

         if not stack:
            continue   # stray #endif -- a different rule's concern

         start_line, cond = stack.pop()
         span = i - start_line + 1

         if span < LONG_BLOCK_LINES:
            continue

         trailing = m_endif.group(1).strip()

         if trailing and _ANNOT_PAT.match(trailing):
            continue   # annotation present -- compliant

         if trailing == '':
            problem = 'bare `#endif` for a {}-line block'.format(span)
         else:
            problem = ('`#endif` trailing content `{}` is not a valid '
                       'annotation').format(trailing[:30])

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=i,
            col=1,
            end_line=i,
            end_col=len(line) + 1,
            rule=self.id,
            severity=self.severity,
            message=('{} -- annotate with `/* {} */` or `// {}` '
                     'to make the block end traceable').format(
               problem, cond or 'CONDITION', cond or 'CONDITION'
            ),
            snippet=line.rstrip(),
            suggestion='#endif /* {} */'.format(cond or 'CONDITION'),
         ))

      return out


RULE = EndifAnnotationLongBlocks()
