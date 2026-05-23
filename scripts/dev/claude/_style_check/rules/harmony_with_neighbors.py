# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=consider-using-f-string

from typing import List

from .base import (
   Rule,
   Diagnostic,
   CheckContext,
   LAYER_RAW_TEXT,
   SEVERITY_WARNING,
   SCORE_SOFT,
)
from .. import tokens as _tokens_mod

# Context-sensitive (Q22b clarification): an under-80-col line can
# still be ugly when it stands out from its short neighbors. This
# rule is intentionally fuzzy -- the tunables below are gentle and
# the diagnostic is SOFT.

# Window radius (lines on each side of the target line).
WINDOW_RADIUS = 4

# Don't bother with target lines shorter than this -- they can't
# create disharmony with reasonably-sized neighbors.
MIN_TARGET_LEN = 60

# Target must exceed neighbors' median by at least this many cols
# to be flagged. Tuned so 70-cols-amid-50-col-block fires while
# 70-cols-amid-65-col-block does not.
MIN_DELTA_FROM_MEDIAN = 18

# Neighbors' median must be at or below this to consider the
# neighborhood "short" enough that disharmony matters.
NEIGHBOR_MAX_MEDIAN = 55

# Need at least this many non-blank neighbors in the window to draw
# a conclusion -- a target line in the middle of a sparse area has
# no real "neighborhood."
MIN_NEIGHBORS = 4


class HarmonyWithNeighbors(Rule):

   id = 'harmony_with_neighbors'
   description = (
      'A line significantly longer than its neighbors creates '
      'visual disharmony even when under 80 cols. Soft, context-'
      'sensitive (Q22b clarification): when in doubt between '
      'squeezing to 80 cols vs wrapping for harmony, prefer the '
      'wrap.'
   )
   layers = LAYER_RAW_TEXT
   severity = SEVERITY_WARNING
   default_score = SCORE_SOFT

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      # Mask out comments and strings so they don't dominate the
      # line-length statistics (a long comment shouldn't drag the
      # neighborhood median up).
      masked = _tokens_mod.mask_non_code(ctx.source_text)
      masked_lines = masked.split('\n')

      # Pre-compute effective code length per line.
      lengths = [len(line.rstrip()) for line in masked_lines]

      out = []

      for i, length in enumerate(lengths):

         if length < MIN_TARGET_LEN:
            continue

         # Build the window excluding the target itself and excluding
         # blank lines / comment-only lines (length 0 in masked).
         lo = max(0, i - WINDOW_RADIUS)
         hi = min(len(lengths), i + WINDOW_RADIUS + 1)

         neighbors = [lengths[j]
                      for j in range(lo, hi)
                      if j != i and lengths[j] > 0]

         if len(neighbors) < MIN_NEIGHBORS:
            continue

         sorted_n = sorted(neighbors)
         median = sorted_n[len(sorted_n) // 2]

         if median > NEIGHBOR_MAX_MEDIAN:
            continue   # neighbors aren't actually short

         delta = length - median

         if delta < MIN_DELTA_FROM_MEDIAN:
            continue

         line_text = ctx.lines[i] if i < len(ctx.lines) else ''

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=i + 1,
            col=1,
            end_line=i + 1,
            end_col=length + 1,
            rule=self.id,
            severity=self.severity,
            message=(
               'line is {} cols; median of {} nearby code lines is '
               '{} cols (+{} delta) -- visual disharmony, consider '
               'wrapping for harmony with neighbors'
            ).format(length, len(neighbors), median, delta),
            snippet=line_text,
            suggestion='wrap the line to fit the local rhythm',
         ))

      return out


RULE = HarmonyWithNeighbors()
