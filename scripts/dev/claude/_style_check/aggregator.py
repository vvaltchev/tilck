# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=broad-except, consider-using-f-string

from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional

import clang.cindex
from clang.cindex import CursorKind

from .rules.base import Diagnostic


# Verdict thresholds (per-function normalized score: total / stmt_count).
# Tuned so a function with one SOFT-MILD nit (-0.5 / 10 stmts = -0.05)
# is "clean", a function with a -3.0 SOFT-STRONG hit per 10 stmts
# (= -0.3) is "drift", and anything worse is "ugly". A single HARD
# violation forces "broken" regardless of stmt count.
VERDICT_CLEAN_FLOOR  = -0.1   # >= floor -> clean
VERDICT_DRIFT_FLOOR  = -0.4   # >= drift floor -> drift; else ugly


@dataclass
class FunctionRegion:
   name: str
   start_line: int
   end_line: int
   stmt_count: int = 0       # rough: non-blank, non-pure-comment lines
   diagnostics: List[Diagnostic] = field(default_factory=list)
   prettiness: float = 1.0   # 0.0..1.0 gradient quality metric

   @property
   def violation_diagnostics(self):
      return [d for d in self.diagnostics if not d.is_gradient]

   @property
   def total_score(self) -> float:
      return sum(d.score for d in self.violation_diagnostics)

   @property
   def normalized_score(self) -> float:
      """Per-statement average score. Negative numbers, lower is uglier.
      Used to compare functions of different sizes on the same scale."""

      if self.stmt_count == 0:
         return 0.0

      return self.total_score / self.stmt_count

   @property
   def hard_violations(self) -> int:
      return sum(
         1 for d in self.diagnostics
         if d.severity == 'error' and not d.is_gradient
      )

   @property
   def hard_failure_gradients(self) -> int:
      """Count of gradient diagnostics whose prettiness_cost reached
      the hard-fail threshold. Each one is a statement that the rule
      flagged as catastrophically ugly."""

      return sum(1 for d in self.diagnostics if d.is_hard_failure
                 and d.is_gradient)

   @property
   def soft_violations(self) -> int:
      return sum(
         1 for d in self.diagnostics
         if d.severity == 'warning' and not d.is_gradient
      )

   @property
   def verdict(self) -> str:
      """Coarse label combining severity + normalized prettiness.
      Hard violation or hard-failure gradient -> broken; otherwise
      depends on normalized score."""

      if self.hard_violations > 0 or self.hard_failure_gradients > 0:
         return 'broken'

      if not self.diagnostics:
         return 'clean'

      ns = self.normalized_score

      if ns >= VERDICT_CLEAN_FLOOR:
         return 'clean'

      if ns >= VERDICT_DRIFT_FLOOR:
         return 'drift'

      return 'ugly'


@dataclass
class FileSummary:
   """Per-file rollup: every function in the file (even those with
   no diagnostics) plus the un-assigned file-level diagnostics
   (e.g. header rules like spdx_header, include_order)."""

   file_path: Path
   functions: List[FunctionRegion] = field(default_factory=list)
   file_level_diagnostics: List[Diagnostic] = field(default_factory=list)

   @property
   def file_prettiness(self) -> float:
      """Weighted mean of per-function prettiness. File-level
      gradient diagnostics reduce a separate pool proportional
      to the file's scoreable line count."""

      if not self.functions:
         return 1.0

      total_stmts = sum(f.stmt_count for f in self.functions)

      if total_stmts == 0:
         return 1.0

      weighted = sum(
         f.prettiness * f.stmt_count for f in self.functions
      )
      base = weighted / total_stmts

      # File-level gradient diagnostics (e.g. ifdef_density)
      # reduce the file score proportionally.
      file_gradient_cost = sum(
         d.prettiness_cost for d in self.file_level_diagnostics
         if d.is_gradient and d.prettiness_cost > 0
      )

      if file_gradient_cost > 0 and total_stmts > 0:
         per_line = file_gradient_cost / total_stmts
         base = base - per_line

      return base

   @property
   def total_score(self) -> float:
      func_total = sum(f.total_score for f in self.functions)
      file_total = sum(d.score for d in self.file_level_diagnostics)
      return func_total + file_total

   @property
   def all_diagnostics(self) -> List[Diagnostic]:
      out = list(self.file_level_diagnostics)
      for f in self.functions:
         out.extend(f.diagnostics)
      return out


def extract_functions(tu, file_path: Path) -> List[FunctionRegion]:
   """Walk the TU and return all function DEFINITIONS in the main
   file with their line ranges. Returns empty list if no TU
   available (rule may have run without libclang)."""

   if tu is None:
      return []

   try:
      main_resolved = str(file_path.resolve())
   except Exception:
      main_resolved = str(file_path)

   out = []
   seen = set()

   for cursor in tu.cursor.walk_preorder():

      if cursor.kind != CursorKind.FUNCTION_DECL:
         continue

      try:

         if not cursor.is_definition():
            continue

      except Exception:
         continue

      loc = cursor.location

      if loc.file is None:
         continue

      try:

         if str(Path(str(loc.file)).resolve()) != main_resolved:
            continue

      except Exception:
         continue

      try:
         start = cursor.extent.start.line
         end = cursor.extent.end.line
      except Exception:
         continue

      key = (cursor.spelling, start, end)

      if key in seen:
         continue

      seen.add(key)

      out.append(FunctionRegion(
         name=cursor.spelling,
         start_line=start,
         end_line=end,
      ))

   out.sort(key=lambda f: f.start_line)
   return out


def _count_statements(lines: List[str], start: int, end: int) -> int:
   """Approximate statement count: non-blank, non-pure-comment,
   non-pure-brace lines in the [start, end] range (1-based inclusive)."""

   cnt = 0

   for ln in range(start, end + 1):

      if ln <= 0 or ln > len(lines):
         continue

      stripped = lines[ln - 1].strip()

      if not stripped:
         continue

      if stripped.startswith('//'):
         continue

      if stripped.startswith('/*') or stripped.startswith('*'):
         continue

      # A line that is JUST a brace or just a brace + comment.
      bare = stripped.rstrip('{};').strip()

      if bare in ('', '{', '}', '} else', '} else if'):
         continue

      cnt += 1

   return max(1, cnt)


_BRACE_ONLY = frozenset((
   '', '{', '}', '};', '})', '});', ');', '))', '));', '}, {',
))


def _compute_prettiness(lines, start, end, diagnostics):
   """Compute per-function prettiness from ALL diagnostics.
   Each scoreable line starts at 1.0; both gradient costs and
   violation scores reduce the affected line. Line scores and the
   final mean are UNCLAMPED -- a HARD violation (-10.0) on one
   line, or a hard-failure gradient (cost >= 3.0), drags the
   function score deep negative, which is the point: catastrophic
   ugliness should be visible as a catastrophic score."""

   line_scores = {}

   for ln in range(start, end + 1):

      if ln <= 0 or ln > len(lines):
         continue

      stripped = lines[ln - 1].strip()

      if not stripped:
         continue

      if stripped in _BRACE_ONLY:
         continue

      line_scores[ln] = 1.0

   if not line_scores:
      return 1.0

   for d in diagnostics:

      cost = d.prettiness_cost

      if cost <= 0 and d.score < 0:
         cost = abs(d.score)

      if cost <= 0:
         continue

      for ln in range(d.line, d.end_line + 1):

         if ln in line_scores:
            line_scores[ln] -= cost

   return sum(line_scores.values()) / len(line_scores)


def build_file_summary(file_path: Path,
                       tu,
                       lines: List[str],
                       diagnostics: List[Diagnostic]) -> FileSummary:

   summary = FileSummary(file_path=file_path)
   funcs = extract_functions(tu, file_path)

   # Compute statement counts up-front.
   for f in funcs:
      f.stmt_count = _count_statements(lines, f.start_line, f.end_line)

   # Assign each diagnostic to its enclosing function, if any.
   # A diag whose line falls inside multiple ranges (nested fns are
   # not legal in C, but defensive) gets attributed to the innermost.
   for d in diagnostics:

      enclosing: Optional[FunctionRegion] = None
      best_size = None

      for f in funcs:

         if not (f.start_line <= d.line <= f.end_line):
            continue

         size = f.end_line - f.start_line

         if best_size is None or size < best_size:
            enclosing = f
            best_size = size

      if enclosing is not None:
         enclosing.diagnostics.append(d)
      else:
         summary.file_level_diagnostics.append(d)

   # Sort each function's diagnostics by line for stable display.
   for f in funcs:
      f.diagnostics.sort(key=lambda d: (d.line, d.col))
      f.prettiness = _compute_prettiness(
         lines, f.start_line, f.end_line, f.diagnostics
      )

   summary.file_level_diagnostics.sort(key=lambda d: (d.line, d.col))
   summary.functions = funcs
   return summary
