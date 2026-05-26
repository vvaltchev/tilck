# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=consider-using-f-string

import re

from collections import defaultdict
from typing import List

from .base import (
   Rule,
   Diagnostic,
   Fix,
   CheckContext,
   LAYER_TOKENS,
   SEVERITY_WARNING,
   SCORE_MEDIUM_PREF,
)
from .. import tokens as _tokens_mod

# Q10: when a boolean expression wraps across multiple lines, the
# connecting operators (`||`, `&&`) should be column-aligned across
# the wrapped lines at the SAME paren nesting depth.  Operators at
# different depths belong to different sub-expressions and are not
# required to align with each other.
#
# Detection: scan masked source for RUNS of consecutive lines that
# each end with `&&`, `||`, `|`, or `&`.  Within a run, compute the
# paren depth at each trailing operator and group by depth.  Within
# each depth group (>= 2 entries), all operators should be at the
# same column.  Two-char operators (`&&`, `||`) are matched first
# so a trailing `||` is not split into `|` + `|`.
_OP_END_PAT = re.compile(r'^(.*?)(&&|\|\||\||\&)\s*$')

# In C++ files, `&&` at end of line in a declaration context is a
# rvalue reference, not a logical operator.  Skip when the line has
# no comparison or other logical operator preceding the trailing `&&`.
_CMP_OR_LOGIC_PAT = re.compile(r'==|!=|<=|>=|<|>|\|\||&&(?![\s]*$)')


def _paren_delta(text):
   d = 0
   for ch in text:
      if ch == '(':
         d += 1
      elif ch == ')':
         d -= 1
   return d


class AlignMultilineOperators(Rule):

   id = 'align_multiline_operators'
   description = (
      'Multi-line boolean expressions: trailing `||` / `&&` should '
      'be column-aligned across the wrapped lines at the same '
      'paren nesting depth (pad with extra space before the '
      'operator if needed). Q10 preference.'
   )
   layers = LAYER_TOKENS
   severity = SEVERITY_WARNING
   default_score = SCORE_MEDIUM_PREF

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      masked = _tokens_mod.mask_non_code(ctx.source_text)
      masked_lines = masked.split('\n')

      runs = []
      current = []

      for i, line in enumerate(masked_lines):

         m = _OP_END_PAT.match(line)

         if not m:

            if len(current) >= 2:
               runs.append(current)

            current = []
            continue

         op = m.group(2)

         # In C++ files, trailing `&&` without any comparison or
         # logical operator on the same line is likely a rvalue
         # reference in a declaration, not a boolean expression.
         # Treat as a non-operator line to break the run.
         if ctx.is_cpp and op == '&&':
            prefix = m.group(1)

            if not _CMP_OR_LOGIC_PAT.search(prefix):

               if len(current) >= 2:
                  runs.append(current)

               current = []
               continue

         op_col = line.rfind(op) + 1
         current.append((i, op_col, op))

      if len(current) >= 2:
         runs.append(current)

      out = []

      for run in runs:

         depth = 0
         by_depth = defaultdict(list)

         for line_idx, op_col, op in run:

            line = masked_lines[line_idx]
            m = _OP_END_PAT.match(line)
            prefix = m.group(1)
            op_depth = depth + _paren_delta(prefix)
            by_depth[op_depth].append((line_idx, op_col, op))
            depth += _paren_delta(line)

         for _, group in by_depth.items():

            if len(group) < 2:
               continue

            cols = [c for _, c, _ in group]
            target_col = max(cols)

            for line_idx, col, op in group:

               if col == target_col:
                  continue

               line_no = line_idx + 1
               line_text = ctx.lines[line_idx] \
                  if line_idx < len(ctx.lines) else ''

               pad = target_col - col
               op_pos = line_text.rfind(op)
               fixed_line = line_text[:op_pos] + ' ' * pad + \
                  line_text[op_pos:]

               out.append(Diagnostic(
                  file=str(ctx.file_path),
                  line=line_no,
                  col=col,
                  end_line=line_no,
                  end_col=col + len(op),
                  rule=self.id,
                  severity=self.severity,
                  message=(
                     'operator `{}` at column {}; other operators '
                     'at same nesting level are at column {} -- '
                     'pad with spaces to align'
                  ).format(op, col, target_col),
                  snippet=line_text,
                  fixes=[Fix(line_no, line_no, [fixed_line])],
               ))

      return out


RULE = AlignMultilineOperators()
