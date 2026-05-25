# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=consider-using-f-string

import re

from typing import List

from .base import (
   COST_MILD,
   Rule,
   Diagnostic,
   CheckContext,
   LAYER_TOKENS,
   SEVERITY_WARNING,
   SCORE_NUDGE,
)
from .. import tokens as _tokens_mod

# Detect visual disharmony between a multi-line function call
# whose last argument is a repeating-operator expression (|, &,
# &&, ||) and a long single-line neighbor (preceding or following
# statement).
#
# Example (ugly):
#
#   foo(a, b, X     |
#                Y  |
#                Z);
#   foo(a, b, P | Q | R | S);      <-- long neighbor
#
# The wrapped call creates a visual bulge among otherwise compact
# single-line calls.  Very soft -- the compacted form might not
# fit in 80 cols, or the author may prefer the wrapped style for
# readability.

_OP_END = re.compile(r'(&&|\|\||\||\&)\s*$')
_CALL_CLOSE = re.compile(r'\)\s*;')
_CALL_OPEN = re.compile(r'\b\w+\s*\(')

LONG_NEIGHBOR = 60
MIN_OP_LINES = 2


class MultilineCallVsLongNeighbor(Rule):

   id = 'multiline_call_vs_long_neighbor'
   description = (
      'A multi-line function call whose last argument is a '
      'repeating-operator expression (|, &, &&, ||) has a long '
      'single-line neighbor -- visual disharmony.  Consider '
      'compacting the expression or wrapping the neighbor. '
      'Very soft (NUDGE).'
   )
   layers = LAYER_TOKENS
   severity = SEVERITY_WARNING
   default_score = SCORE_NUDGE

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      masked = _tokens_mod.mask_non_code(ctx.source_text)
      masked_lines = masked.split('\n')
      out = []
      skip_to = 0

      for i in range(len(masked_lines)):

         if i < skip_to:
            continue

         line = masked_lines[i]

         if not _CALL_OPEN.search(line):
            continue

         if not _OP_END.search(line):
            continue

         op_lines = 1
         j = i + 1
         found_close = False

         while j < len(masked_lines):

            if _OP_END.search(masked_lines[j]):
               op_lines += 1
               j += 1
               continue

            if _CALL_CLOSE.search(masked_lines[j]):
               found_close = True
               j += 1
               break

            break

         call_end = j
         skip_to = call_end

         if not found_close or op_lines < MIN_OP_LINES:
            continue

         indent = len(masked_lines[i]) - len(masked_lines[i].lstrip())
         parts = [masked_lines[k].strip() for k in range(i, call_end)]
         compacted = re.sub(r'\s+', ' ', ' '.join(parts))

         if indent + len(compacted) > 80:
            continue

         long_before = False
         long_after = False

         prev = i - 1

         while prev >= 0 and not masked_lines[prev].strip():
            prev -= 1

         if prev >= 0:
            prev_len = len(masked_lines[prev].rstrip())
            long_before = (prev_len >= LONG_NEIGHBOR)

         nxt = call_end

         while nxt < len(masked_lines) and \
               not masked_lines[nxt].strip():
            nxt += 1

         if nxt < len(masked_lines):
            nxt_len = len(masked_lines[nxt].rstrip())
            long_after = (nxt_len >= LONG_NEIGHBOR)

         if not long_before and not long_after:
            continue

         line_no = i + 1
         line_text = ctx.lines[i] if i < len(ctx.lines) else ''

         if long_after:
            neighbor_line = nxt + 1
         else:
            neighbor_line = prev + 1

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=line_no,
            col=1,
            end_line=call_end,
            end_col=1,
            rule=self.id,
            severity=self.severity,
            message=(
               'multi-line call (lines {}-{}) with repeating-'
               'operator last arg has a long single-line '
               'neighbor at line {} -- visual disharmony, '
               'consider compacting or wrapping uniformly'
            ).format(line_no, call_end, neighbor_line),
            snippet=line_text.rstrip(),
            is_gradient=True,
            prettiness_cost=COST_MILD,
         ))

      return out


RULE = MultilineCallVsLongNeighbor()
