# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-function-docstring
"""
The harmony model: which lines of a function are too long for their
neighbours, whatever the language.

A function body is two series of line lengths: every line the eye sees
(code and comments alike), and the "normal" lines among them, the ones a
writer could have shaped differently (not declarations, signatures,
continuation lines, ...). What counts as normal is the language's call;
the statistics are the same for every language:

  1. an IQR fence over the visible lengths (robust to the outliers it
     looks for);
  2. a normal line beyond the fence, long in absolute terms and far
     from the median, is an outlier,
  3. unless it is part of an alternating long-short rhythm, or has a
     length partner (another line about as long) in the function, or
     the language exempts it (a printf, a string-heavy line, ...).

The cost grows from COST_MILD at the fence to COST_SIGNIFICANT at twice
the fence's distance from the median.

Codebase analysis behind the thresholds (477 C files, 3304 functions,
40k code lines): 20% of consecutive-line transitions differ by more than
30 columns, golden files included; mixing categories of lines (printf
~53, calls ~43, return ~24) is what makes most of the variation, and it
is structural, not accidental.
"""

from dataclasses import dataclass
from typing import Callable, List, Sequence, Set, Tuple

from .rules.base import COST_MILD, COST_SIGNIFICANT

MIN_TARGET_LEN = 68
MIN_DELTA_FROM_MEDIAN = 30
IQR_MULTIPLIER = 3.0
MIN_IQR = 8
MIN_NORMAL_LINES = 6
PARTNER_RANGE = 12
MAX_PARTNERS = 0
ALTERNATING_MIN_DELTA = 12
ALTERNATING_MIN_RUN = 2


@dataclass
class Outlier:

   line: int
   length: int
   median: float
   fence: float
   cost: float

   def message(self) -> str:
      delta = int(self.length - self.median)

      return ('line is {} cols; function median {:.0f} (+{} delta); '
              'IQR fence {:.0f}').format(self.length, self.median, delta,
                                         self.fence)


def outliers(visual_lens: Sequence[int],
             normal: Sequence[Tuple[int, int]],
             exempt: Callable[[int], bool]) -> List[Outlier]:

   """The outliers among `normal` ((line, length) pairs) of a function
   whose visible lines have lengths `visual_lens`. `exempt(line)` says
   whether the language excuses a line (it is asked only about lines
   that are outliers otherwise)."""

   if len(normal) < MIN_NORMAL_LINES or len(visual_lens) < MIN_NORMAL_LINES:
      return []

   fence = iqr_fence(visual_lens)
   med = median(visual_lens)
   alternating = detect_alternating(normal)
   out = []

   for line_no, length in normal:

      if length < MIN_TARGET_LEN or length <= fence:
         continue

      if length - med < MIN_DELTA_FROM_MEDIAN:
         continue

      if line_no in alternating or exempt(line_no):
         continue

      partners = sum(
         1 for vl in visual_lens if abs(vl - length) <= PARTNER_RANGE
      ) - 1

      if partners > MAX_PARTNERS:
         continue

      overshoot = (length - fence) / max(fence - med, 1)
      cost = min(
         COST_SIGNIFICANT,
         COST_MILD + (COST_SIGNIFICANT - COST_MILD) * overshoot,
      )

      out.append(Outlier(line_no, length, med, fence, cost))

   return out


def detect_alternating(normal_entries: Sequence[Tuple[int, int]]) -> Set[int]:

   """Line numbers that are part of an alternating long-short-long-short
   pattern in a sequence of (line_no, length) pairs."""

   if len(normal_entries) < 4:
      return set()

   n = len(normal_entries)
   deltas = [
      normal_entries[i + 1][1] - normal_entries[i][1]
      for i in range(n - 1)
   ]

   # alt[i] = True when deltas[i] and deltas[i+1] have opposite
   # sign and both are large enough to matter
   alt = []

   for i in range(len(deltas) - 1):
      alt.append(
         deltas[i] * deltas[i + 1] < 0
         and abs(deltas[i]) >= ALTERNATING_MIN_DELTA
         and abs(deltas[i + 1]) >= ALTERNATING_MIN_DELTA
      )

   patterned = set()
   i = 0

   while i < len(alt):

      if not alt[i]:
         i += 1
         continue

      j = i

      while j < len(alt) and alt[j]:
         j += 1

      if j - i >= ALTERNATING_MIN_RUN:

         for k in range(i, min(j + 2, n)):
            patterned.add(normal_entries[k][0])

      i = j

   return patterned


def iqr_fence(lengths: Sequence[int]) -> float:

   n = len(lengths)

   if n < 4:
      return 999.0

   s = sorted(lengths)
   q1 = s[n // 4]
   q3 = s[3 * n // 4]
   iqr = max(q3 - q1, MIN_IQR)
   return q3 + IQR_MULTIPLIER * iqr


def median(lengths: Sequence[int]) -> float:

   s = sorted(lengths)
   return float(s[len(s) // 2])
