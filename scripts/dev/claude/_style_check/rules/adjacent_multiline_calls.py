# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=consider-using-f-string

import re

from typing import List, Optional, Tuple

from .base import (
   Rule,
   Diagnostic,
   CheckContext,
   LAYER_TOKENS,
   SEVERITY_WARNING,
   COST_MODERATE,
)
from .. import tokens as _tokens_mod

# Two adjacent multi-line call/assignment constructs (no blank line
# between them) look visually packed when at least one uses style 1
# (first arg on opening-paren line, `);` glued to last arg).
#
# When both use style 2 (`(` ends a line, `);` on its own line),
# the `);` line acts as visual breathing room -- no blank needed.
#
# Additionally, mixing style 1 and style 2 between adjacent calls
# creates visual disharmony: different shapes butted together.
#
# Example (flagged -- style 1 then style 2, no blank line):
#
#   bintree_insert(&timer_tree_root,
#                  ti,
#                  timer_cmp);
#   earliest = bintree_get_first_obj(
#      timer_tree_root, struct task, timer_tree_node
#   );
#
# Fixed (same style + blank line):
#
#   bintree_insert(&timer_tree_root,
#                  ti,
#                  timer_cmp);
#
#   earliest = bintree_get_first_obj(timer_tree_root,
#                                    struct task,
#                                    timer_tree_node);

_CALL_RE = re.compile(r'\b\w+\s*\(')


def _line_offset(masked_lines, line_idx):
   return sum(len(masked_lines[k]) + 1 for k in range(line_idx))


def _find_call_on_line(masked, masked_lines, line_idx):
   """Find the outermost multi-line call starting on line_idx.

   Returns (end_line, style) or None if no multi-line call starts
   on this line.  style is 1 or 2.
   """
   line = masked_lines[line_idx]

   for m in _CALL_RE.finditer(line):

      open_col = line.index('(', m.start())
      open_ofs = _line_offset(masked_lines, line_idx) + open_col
      close_ofs = _tokens_mod.find_matching_close(masked, open_ofs)

      if close_ofs < 0:
         continue

      close_line = masked[:close_ofs].count('\n')

      if close_line <= line_idx:
         continue

      after_open = line[open_col + 1:].strip()

      if after_open:
         return (close_line, 1)

      close_text = masked_lines[close_line]
      before_close_col = close_text.index(')')
      before_close = close_text[:before_close_col].strip()

      if not before_close:
         return (close_line, 2)

      return (close_line, 1)

   return None


class AdjacentMultilineCalls(Rule):

   id = 'adjacent_multiline_calls'
   description = (
      'Two adjacent multi-line calls without a blank line between '
      'them: when at least one uses style 1 (args aligned to '
      'paren, ); glued to last arg), add a blank separator. '
      'Style 2 pairs (where ); sits on its own line) are fine '
      'because the ); line provides visual breathing room. '
      'Mixing styles between adjacent calls also creates '
      'disharmony.'
   )
   layers = LAYER_TOKENS
   severity = SEVERITY_WARNING
   default_score = 0.0

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      masked = _tokens_mod.mask_non_code(ctx.source_text)
      masked_lines = masked.split('\n')
      out = []
      skip_to = 0

      for i in range(len(masked_lines)):

         if i < skip_to:
            continue

         result = _find_call_on_line(masked, masked_lines, i)

         if result is None:
            continue

         end_a, style_a = result
         skip_to = end_a + 1

         next_idx = end_a + 1

         if next_idx >= len(masked_lines):
            continue

         if not masked_lines[next_idx].strip():
            continue

         result_b = _find_call_on_line(
            masked, masked_lines, next_idx
         )

         if result_b is None:
            continue

         _, style_b = result_b

         if style_a == 2 and style_b == 2:
            continue

         mixed = (style_a != style_b)
         line_no = next_idx + 1
         line_text = ctx.lines[next_idx] if next_idx < len(ctx.lines) else ''

         if mixed:
            msg = (
               'adjacent multi-line calls use different styles '
               '(style {} then style {}) with no blank line -- '
               'use the same style and add a blank separator'
            ).format(style_a, style_b)
         else:
            msg = (
               'adjacent multi-line calls (both style 1) with '
               'no blank line between them -- add a blank line '
               'for visual breathing room'
            )

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=line_no,
            col=1,
            end_line=line_no,
            end_col=1,
            rule=self.id,
            severity=self.severity,
            message=msg,
            snippet=line_text.rstrip(),
            is_gradient=True,
            prettiness_cost=COST_MODERATE,
         ))

      return out


RULE = AdjacentMultilineCalls()
