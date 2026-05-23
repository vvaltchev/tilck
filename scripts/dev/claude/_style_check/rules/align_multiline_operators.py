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
   SCORE_MEDIUM_PREF,
)
from .. import tokens as _tokens_mod

# Q10: when a boolean expression wraps across multiple lines, the
# connecting operators (`||`, `&&`) should be column-aligned across
# the wrapped lines. Pad with extra space before the operator if
# needed.
#
# Detection: scan masked source for RUNS of consecutive lines that
# each end with `&&` or `||`. Within a run, all operators should be
# at the same column. If they aren't, flag the misaligned ones.
# `break_before_operator_forbidden` (HARD) already handles the
# "operator at start of line" form; this rule applies only to the
# break-after-operator shape.
_OP_END_PAT = re.compile(r'^(.*?)(&&|\|\|)\s*$')


class AlignMultilineOperators(Rule):

   id = 'align_multiline_operators'
   description = (
      'Multi-line boolean expressions: trailing `||` / `&&` should '
      'be column-aligned across the wrapped lines (pad with extra '
      'space before the operator if needed). Q10 preference.'
   )
   layers = LAYER_TOKENS
   severity = SEVERITY_WARNING
   default_score = SCORE_MEDIUM_PREF

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      masked = _tokens_mod.mask_non_code(ctx.source_text)
      masked_lines = masked.split('\n')

      # Group consecutive lines ending with && / || into runs.
      runs = []         # list of [(line_idx, op_col, op_str)]
      current = []

      for i, line in enumerate(masked_lines):

         m = _OP_END_PAT.match(line)

         if not m:

            if len(current) >= 2:
               runs.append(current)

            current = []
            continue

         op = m.group(2)
         # Op column is 1-based; find the last occurrence of op
         # since the match is at end of line.
         op_col = line.rfind(op) + 1
         current.append((i, op_col, op))

      if len(current) >= 2:
         runs.append(current)

      out = []

      for run in runs:

         cols = [c for _, c, _ in run]

         # The "target" column for this run is the rightmost --
         # padding adds spaces, never removes them, so the leftmost
         # ops are the misaligned ones.
         target_col = max(cols)

         for line_idx, col, op in run:

            if col == target_col:
               continue

            line_no = line_idx + 1
            line_text = ctx.lines[line_idx] \
               if line_idx < len(ctx.lines) else ''

            out.append(Diagnostic(
               file=str(ctx.file_path),
               line=line_no,
               col=col,
               end_line=line_no,
               end_col=col + len(op),
               rule=self.id,
               severity=self.severity,
               message=(
                  'operator `{}` at column {}; other operators in '
                  'this wrapped expression are at column {} -- pad '
                  'with spaces to align'
               ).format(op, col, target_col),
               snippet=line_text,
            ))

      return out


RULE = AlignMultilineOperators()
