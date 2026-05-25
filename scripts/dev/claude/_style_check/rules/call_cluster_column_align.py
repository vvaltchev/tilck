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

# Detect consecutive lines calling the same function (or macro)
# where the last argument column drifts — some lines have it
# aligned, others don't.  This catches the common pattern of
# column-aligned call tables where one entry is mis-padded.
#
# Detection:
# 1. Group consecutive non-blank lines sharing the same call
#    prefix `func_name(`.
# 2. For each group (>= 3 lines), find the column of each
#    top-level comma that separates the last two arguments.
# 3. If the majority share the same column but outliers differ,
#    flag the outliers.

_CALL_PREFIX = re.compile(r'^(\s*\w+\s*\()')

MIN_CLUSTER = 3


def _last_arg_start(line):
   """Return the 0-based column where the last top-level argument
   starts (first non-space after the last comma at depth 1),
   or -1 if not found."""

   depth = 0
   last_comma = -1

   for i, ch in enumerate(line):

      if ch == '(':
         depth += 1
      elif ch == ')':
         depth -= 1
      elif ch == ',' and depth == 1:
         last_comma = i

   if last_comma < 0:
      return -1

   for i in range(last_comma + 1, len(line)):

      if line[i] != ' ':
         return i

   return -1


class CallClusterColumnAlign(Rule):

   id = 'call_cluster_column_align'
   description = (
      'Consecutive calls to the same function should have their '
      'last-argument columns aligned.  A drifted entry in an '
      'otherwise column-aligned cluster is flagged.'
   )
   layers = LAYER_TOKENS
   severity = SEVERITY_WARNING
   default_score = SCORE_SOFT

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      masked = _tokens_mod.mask_non_code(ctx.source_text)
      masked_lines = masked.split('\n')
      out = []

      i = 0

      while i < len(masked_lines):

         m = _CALL_PREFIX.match(masked_lines[i])

         if not m:
            i += 1
            continue

         prefix = m.group(1).rstrip()
         func_end = prefix.rfind('(')
         func_name = prefix[:func_end].strip()

         cluster = []
         j = i

         while j < len(masked_lines):

            mj = _CALL_PREFIX.match(masked_lines[j])

            if not mj:
               break

            pj = mj.group(1).rstrip()
            fj_end = pj.rfind('(')
            fj_name = pj[:fj_end].strip()

            if fj_name != func_name:
               break

            arg_col = _last_arg_start(masked_lines[j])

            if arg_col < 0:
               break

            cluster.append((j, arg_col))
            j += 1

         i = max(j, i + 1)

         if len(cluster) < MIN_CLUSTER:
            continue

         cols = [c for _, c in cluster]
         col_counts = {}

         for c in cols:
            col_counts[c] = col_counts.get(c, 0) + 1

         majority_col = max(col_counts, key=col_counts.get)
         majority_n = col_counts[majority_col]

         if majority_n == len(cluster):
            continue

         if majority_n * 3 < len(cluster) * 2:
            continue

         for ln_idx, col in cluster:

            if col == majority_col:
               continue

            if abs(col - majority_col) > 3:
               continue

            line_no = ln_idx + 1
            line_text = ctx.lines[ln_idx] \
               if ln_idx < len(ctx.lines) else ''

            out.append(Diagnostic(
               file=str(ctx.file_path),
               line=line_no,
               col=col + 1,
               end_line=line_no,
               end_col=col + 2,
               rule=self.id,
               severity=self.severity,
               message=(
                  'last arg starts at column {} but the cluster '
                  'majority starts at column {} ({}/{} lines) -- '
                  'realign for visual consistency'
               ).format(col + 1, majority_col + 1,
                        majority_n, len(cluster)),
               snippet=line_text.rstrip(),
            ))

      return out


RULE = CallClusterColumnAlign()
