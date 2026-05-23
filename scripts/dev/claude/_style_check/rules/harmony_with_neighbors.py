# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=broad-except, consider-using-f-string

import re

from pathlib import Path
from typing import List, Set

import clang.cindex
from clang.cindex import CursorKind

from .base import (
   Rule,
   Diagnostic,
   CheckContext,
   SEVERITY_WARNING,
   SCORE_SOFT,
)
from .. import tokens as _tokens_mod

# Q22b clarification: an under-80-col line can still be ugly when it
# stands out from short neighbors. But "neighbors" must be a fair
# comparison set -- declarations, braces, function signatures, and
# multi-line statement continuations all have their own layout
# context and don't represent "statement density".
#
# Classification strategy:
#
#   - Lines that libclang identifies as inside a DECL_STMT, a
#     PARM_DECL list (function signature), or a multi-line CALL_EXPR
#     argument continuation are classified `decl` / `sig` / `cont`
#     and excluded from BOTH the target set and the neighbor set.
#   - Text-based classification handles `brace` (lines that are just
#     `{`, `}`, `}; ...`), `preproc` (`#...`), and `label`
#     (goto/case labels) before the libclang lookup.
#   - Everything else is `normal`.
#
# Thresholds tightened from the initial version: MIN_TARGET_LEN 70,
# MIN_DELTA 35, NEIGHBOR_MAX_MEDIAN 30. Window narrowed to ±3 since
# the user's stated principle ("difference with the line below")
# is essentially about immediate neighbors.

# Window of neighbors considered for the comparison.
WINDOW_RADIUS = 4

# Target line must be at least this long to be a candidate.
MIN_TARGET_LEN = 70

# Target must exceed the neighbor median by at least this many cols.
MIN_DELTA_FROM_MEDIAN = 40

# Neighbors' median must be at or below this to count as "short".
NEIGHBOR_MAX_MEDIAN = 25

# Need at least this many normal-kind neighbors -- so the
# "neighborhood is short" claim is statistically defensible.
MIN_NEIGHBORS = 5

# A "natural function call" with many args is legitimately long; if
# the target line has at least this many commas, it's a busy
# call/expression and disharmony is the wrong framing. The user can
# wrap multi-arg calls if they want; we don't NUDGE there.
MAX_COMMAS_IN_TARGET = 4


# Brace-only lines (just braces, optionally with semicolon).
_BRACE_ONLY_LINES = {
   '{', '}', '};', '})', '});', ');', '))', '));', '}, {',
}

# Labels (goto labels, case, default).
_LABEL_PAT = re.compile(
   r'^\s*(?:case\s+[^:]+|default|[A-Za-z_]\w*)\s*:\s*$'
)

_PREPROC_PAT = re.compile(r'^\s*#')

# Text-based decl: `<modifier...> <type> [*]<name> [= ...];` lines.
# Matches both `int x;` and `const u32 x = 4;`. Conservative: must
# look like a single-line declaration with no `(` (to avoid
# matching function prototypes -- those are caught by _FN_SIG_PAT).
_DECL_LINE_PAT = re.compile(
   r'^\s*'
   r'(?:(?:static|extern|const|volatile|register|inline|signed|unsigned)'
   r'\s+)*'
   r'(?:struct\s+|union\s+|enum\s+)?'
   r'\w+'
   r'(?:\s*\*+\s*|\s+\**)'
   r'\w+'
   r'(?:\s*\[[^\]]*\])*'
   r'\s*(?:=.*)?;\s*$'
)

# Text-based function signature: line starts at col 1 (or with only
# leading whitespace if it's a top-level declaration's name line),
# contains a `(`, and either has no `;` (definition) or ends with
# `);` (prototype). The pattern is intentionally broad -- the
# important property is "this line is part of a function
# signature, NOT a function body line."
_FN_SIG_PAT = re.compile(r'^[A-Za-z_][\w\s\*]*\(')


def _text_classify(raw_line: str, masked_line: str) -> str:
   """Classify by text alone. Returns one of: 'blank', 'brace',
   'preproc', 'label', 'decl', 'fn_sig', or 'normal' (default)."""

   stripped = raw_line.strip()
   masked_stripped = masked_line.strip()

   if not masked_stripped:
      return 'blank'

   if stripped in _BRACE_ONLY_LINES:
      return 'brace'

   if _PREPROC_PAT.match(masked_line):
      return 'preproc'

   if _LABEL_PAT.match(masked_line):
      return 'label'

   # Function signature heuristic: starts at column 1 (no leading
   # whitespace) AND contains `(`. Most function definitions and
   # prototypes match this. Function calls inside a function body
   # have leading indent so they don't match.
   if _FN_SIG_PAT.match(masked_line):
      return 'fn_sig'

   # Single-line declaration without function-call paren.
   if '(' not in masked_line and _DECL_LINE_PAT.match(masked_line):
      return 'decl'

   return 'normal'


def _collect_decl_lines(tu, file_path: Path) -> Set[int]:
   """Lines that libclang marks as DECL_STMT in the main file --
   covers `int x;` AND `int x = init();` uniformly. We add the
   FULL extent (start..end) so multi-line decls are all marked."""

   if tu is None:
      return set()

   try:
      main = str(file_path.resolve())
   except Exception:
      return set()

   out: Set[int] = set()

   for cursor in tu.cursor.walk_preorder():

      if cursor.kind != CursorKind.DECL_STMT:
         continue

      loc = cursor.location

      if loc.file is None:
         continue

      try:

         if str(Path(str(loc.file)).resolve()) != main:
            continue

      except Exception:
         continue

      try:
         start = cursor.extent.start.line
         end = cursor.extent.end.line
      except Exception:
         continue

      for ln in range(start, end + 1):
         out.add(ln)

   return out


def _collect_signature_lines(tu, file_path: Path) -> Set[int]:
   """Lines that belong to a function signature -- from the
   function's start line to the line BEFORE its body's COMPOUND_STMT
   opens. Catches both definitions and (top-level) prototypes."""

   if tu is None:
      return set()

   try:
      main = str(file_path.resolve())
   except Exception:
      return set()

   out: Set[int] = set()

   for cursor in tu.cursor.walk_preorder():

      if cursor.kind != CursorKind.FUNCTION_DECL:
         continue

      loc = cursor.location

      if loc.file is None:
         continue

      try:

         if str(Path(str(loc.file)).resolve()) != main:
            continue

      except Exception:
         continue

      try:
         start = cursor.extent.start.line
      except Exception:
         continue

      # End of signature = first COMPOUND_STMT child's start - 1
      # for a definition, or the extent end (the `;`) for a
      # prototype.
      sig_end = None

      try:
         is_def = cursor.is_definition()
      except Exception:
         is_def = False

      if is_def:

         for ch in cursor.get_children():

            if ch.kind == CursorKind.COMPOUND_STMT:
               sig_end = ch.extent.start.line - 1
               break

      if sig_end is None:

         try:
            sig_end = cursor.extent.end.line
         except Exception:
            continue

      for ln in range(start, sig_end + 1):
         out.add(ln)

   return out


def _collect_continuation_lines(tu, file_path: Path) -> Set[int]:
   """Lines that are NOT the start line of a top-level statement-
   ish cursor but lie inside its extent. These are continuations
   of a multi-line statement (e.g., the 2nd/3rd line of a
   wrapped function call). Excluding them from the harmony check
   avoids comparing one half of a printk to its short call-site
   neighbors."""

   if tu is None:
      return set()

   try:
      main = str(file_path.resolve())
   except Exception:
      return set()

   out: Set[int] = set()

   # Cursors that represent "a complete statement worth of code"
   # at top of a function body.
   STMT_KINDS = {
      CursorKind.CALL_EXPR, CursorKind.IF_STMT,
      CursorKind.WHILE_STMT, CursorKind.FOR_STMT,
      CursorKind.DO_STMT, CursorKind.SWITCH_STMT,
      CursorKind.RETURN_STMT, CursorKind.BINARY_OPERATOR,
      CursorKind.COMPOUND_ASSIGNMENT_OPERATOR,
   }

   for cursor in tu.cursor.walk_preorder():

      if cursor.kind not in STMT_KINDS:
         continue

      loc = cursor.location

      if loc.file is None:
         continue

      try:

         if str(Path(str(loc.file)).resolve()) != main:
            continue

      except Exception:
         continue

      try:
         start = cursor.extent.start.line
         end = cursor.extent.end.line
      except Exception:
         continue

      if end <= start:
         continue

      # Mark every line AFTER the start as a continuation.
      for ln in range(start + 1, end + 1):
         out.add(ln)

   return out


class HarmonyWithNeighbors(Rule):

   id = 'harmony_with_neighbors'
   description = (
      'A line significantly longer than its NORMAL neighbors '
      '(statements only -- declarations, braces, preprocessor, '
      'function signatures, and multi-line statement '
      'continuations are excluded) creates visual disharmony '
      'even when under 80 cols. SOFT context-sensitive (Q22b '
      'clarification): when in doubt between squeezing to 80 cols '
      'and wrapping for harmony, prefer the wrap.'
   )
   layers = 'S+R'
   needs_tu = True
   severity = SEVERITY_WARNING
   default_score = SCORE_SOFT

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      masked = _tokens_mod.mask_non_code(ctx.source_text)
      masked_lines = masked.split('\n')

      lengths = [len(line.rstrip()) for line in masked_lines]

      # Text-based classification (fast, no TU needed).
      kinds = [
         _text_classify(ctx.lines[i] if i < len(ctx.lines) else '',
                        masked_lines[i])
         for i in range(len(masked_lines))
      ]

      # libclang-based overrides for decl / signature / continuation
      # (must take precedence over `normal` text classification).
      decl_lines = _collect_decl_lines(ctx.tu, ctx.file_path)
      sig_lines = _collect_signature_lines(ctx.tu, ctx.file_path)
      cont_lines = _collect_continuation_lines(ctx.tu, ctx.file_path)

      for i in range(len(kinds)):

         line_no = i + 1

         if line_no in decl_lines:
            kinds[i] = 'decl'

         elif line_no in sig_lines:
            kinds[i] = 'sig'

         elif line_no in cont_lines and kinds[i] == 'normal':
            kinds[i] = 'cont'

      out = []

      for i, length in enumerate(lengths):

         if length < MIN_TARGET_LEN:
            continue

         if kinds[i] != 'normal':
            continue

         line_masked = masked_lines[i]
         masked_stripped = line_masked.rstrip()

         # Array/struct initializer entry: line ends with `,` and
         # is part of a multi-line list. The neighbors are other
         # entries; comparing entry lengths isn't disharmony --
         # it's the user's choice of column alignment.
         if masked_stripped.endswith(','):
            continue

         # Skip multi-arg calls / expressions: the line is long
         # because of arg density, not because the author chose
         # to squeeze a small thing into one line.
         if line_masked.count(',') > MAX_COMMAS_IN_TARGET:
            continue

         lo = max(0, i - WINDOW_RADIUS)
         hi = min(len(lengths), i + WINDOW_RADIUS + 1)

         neighbors = [lengths[j]
                      for j in range(lo, hi)
                      if j != i and kinds[j] == 'normal']

         if len(neighbors) < MIN_NEIGHBORS:
            continue

         sorted_n = sorted(neighbors)
         median = sorted_n[len(sorted_n) // 2]

         if median > NEIGHBOR_MAX_MEDIAN:
            continue

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
               'line is {} cols; median of {} nearby normal '
               'statements is {} cols (+{} delta) -- visual '
               'disharmony, consider wrapping'
            ).format(length, len(neighbors), median, delta),
            snippet=line_text,
            suggestion='wrap the line to fit the local rhythm',
         ))

      return out


RULE = HarmonyWithNeighbors()
