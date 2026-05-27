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
   LAYER_TOKENS,
   SEVERITY_WARNING,
   SCORE_SOFT,
)
from .. import tokens as _tokens_mod

# Detect a multi-line function/macro call (style 1: args aligned to
# opening paren) where multiple arguments are packed per line, AND
# the call is immediately followed by a non-trivial statement --
# not just `}` or a blank line.
#
# Example (flagged):
#
#   bintree_remove(&timer_tree_root, ti, timer_cmp,
#                  struct task, timer_tree_node);
#   bintree_node_init(&ti->timer_tree_node);      <-- non-short
#
# Preferred (one arg per line):
#
#   bintree_remove(&timer_tree_root,
#                  ti,
#                  timer_cmp,
#                  struct task,
#                  timer_tree_node);
#   bintree_node_init(&ti->timer_tree_node);
#
# If the line after `);` is just `}` or blank, the packed form
# is acceptable because there is no visual clash.

_CALL_START = re.compile(r'\b(\w+)\s*\(')
_ONLY_BRACE = re.compile(r'^\s*\}\s*$')

MIN_FOLLOWING_LEN = 20


def _count_top_level_commas(masked, start, end):
   depth = 0
   count = 0
   for i in range(start, end):
      ch = masked[i]
      if ch == '(':
         depth += 1
      elif ch == ')':
         depth -= 1
      elif ch == ',' and depth == 1:
         count += 1
   return count


class PackedMultilineBeforeStatement(Rule):

   id = 'packed_multiline_before_stmt'
   description = (
      'A multi-line call (style 1) with multiple args packed per '
      'line is followed by a non-trivial statement. Use one arg '
      'per line to reduce visual clutter.'
   )
   layers = LAYER_TOKENS
   severity = SEVERITY_WARNING
   default_score = SCORE_SOFT

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      masked = _tokens_mod.mask_non_code(ctx.source_text)
      masked_lines = masked.split('\n')
      out = []

      for i in range(len(masked_lines) - 1):

         line = masked_lines[i]
         stripped = line.rstrip()

         if not stripped.endswith(','):
            continue

         m = _CALL_START.search(line)

         if not m:
            continue

         open_paren_col = line.index('(', m.start())
         open_paren_ofs = sum(
            len(masked_lines[k]) + 1 for k in range(i)
         ) + open_paren_col

         close_ofs = _tokens_mod.find_matching_close(
            masked, open_paren_ofs
         )

         if close_ofs < 0:
            continue

         close_line_idx = masked[:close_ofs].count('\n')

         if close_line_idx <= i:
            continue

         call_lines = close_line_idx - i + 1
         top_commas = _count_top_level_commas(
            masked, open_paren_ofs, close_ofs
         )
         arg_count = top_commas + 1

         if arg_count <= call_lines:
            continue

         following_idx = close_line_idx + 1

         if following_idx >= len(masked_lines):
            continue

         following = masked_lines[following_idx].strip()

         if not following:
            continue

         if _ONLY_BRACE.match(masked_lines[following_idx]):
            continue

         if len(following) < MIN_FOLLOWING_LEN:
            continue

         line_no = i + 1
         line_text = ctx.lines[i] if i < len(ctx.lines) else ''

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=line_no,
            col=m.start() + 1,
            end_line=close_line_idx + 1,
            end_col=1,
            rule=self.id,
            severity=self.severity,
            message=(
               'multi-line call with {} args packed into {} lines '
               'is followed by a non-trivial statement at line {}; '
               'use one arg per line for visual clarity'
            ).format(arg_count, call_lines, following_idx + 1),
            snippet=line_text.rstrip(),
         ))

      return out


RULE = PackedMultilineBeforeStatement()
