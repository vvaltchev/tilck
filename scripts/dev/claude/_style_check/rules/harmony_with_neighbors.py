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
   COST_MILD,
   COST_SIGNIFICANT,
)
from .. import tokens as _tokens_mod

# Redesigned around per-function IQR-based outlier detection.
#
# Codebase analysis (477 files, 3304 functions, 40k code lines):
#
#   - 20% of consecutive-line transitions have delta > 30 naturally
#   - Golden files show the same profile (max deltas 60-73)
#   - Category mixing (printf~53, func_call~43, return~24) is the
#     primary driver of variation -- structural, not accidental
#   - 50% of functions are "varied" (CV > 0.45)
#
# The old approach (compare line to neighbor-window median) flags
# the natural rhythm of C code. The new approach:
#
#   1. Segment by function body
#   2. Compute IQR fence per function (robust to outliers)
#   3. Flag lines beyond the fence ONLY if they survive all
#      pattern filters (printf, string-heavy, alternating,
#      initializer, multi-arg, length partners)

# ── Thresholds ───────────────────────────────────────────────────

MIN_TARGET_LEN = 68
MIN_DELTA_FROM_MEDIAN = 30
IQR_MULTIPLIER = 3.0
MIN_IQR = 8
MIN_NORMAL_LINES = 6
PARTNER_RANGE = 12
MAX_PARTNERS = 0
MAX_COMMAS = 4
ALTERNATING_MIN_DELTA = 12
ALTERNATING_MIN_RUN = 2


# ── Category detection ───────────────────────────────────────────

_PRINTF_PAT = re.compile(
   r'^\s*(?:printf|fprintf|snprintf|printk|'
   r'pr_info|pr_err|pr_warn|pr_debug|dprintk|panic)\s*\('
)

_STRING_LITERAL_PAT = re.compile(r'"(?:[^"\\]|\\.)*"')


def _is_printf_line(raw_line):
   return bool(_PRINTF_PAT.match(raw_line))


def _is_string_heavy(raw_line):
   stripped = raw_line.strip()
   if not stripped:
      return False
   total = sum(
      len(m.group()) for m in _STRING_LITERAL_PAT.finditer(stripped)
   )
   return total > len(stripped) * 0.40


# ── Text-based line classification ───────────────────────────────

_BRACE_ONLY_LINES = {
   '{', '}', '};', '})', '});', ');', '))', '));', '}, {',
}

_LABEL_PAT = re.compile(
   r'^\s*(?:case\s+[^:]+|default|[A-Za-z_]\w*)\s*:\s*$'
)
_PREPROC_PAT = re.compile(r'^\s*#')
_DECL_LINE_PAT = re.compile(
   r'^\s*'
   r'(?:(?:static|extern|const|volatile|register|inline'
   r'|signed|unsigned)\s+)*'
   r'(?:struct\s+|union\s+|enum\s+)?'
   r'\w+'
   r'(?:\s*\*+\s*|\s+\**)'
   r'\w+'
   r'(?:\s*\[[^\]]*\])*'
   r'\s*(?:=.*)?;\s*$'
)
_FN_SIG_PAT = re.compile(r'^[A-Za-z_][\w\s\*]*\(')


def _text_classify(raw_line, masked_line):

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

   if _FN_SIG_PAT.match(masked_line):
      return 'fn_sig'

   if '(' not in masked_line and _DECL_LINE_PAT.match(masked_line):
      return 'decl'

   return 'normal'


# ── libclang helpers ─────────────────────────────────────────────

def _resolve_main(file_path):

   try:
      return str(file_path.resolve())
   except Exception:
      return None


def _is_main_file(cursor_loc, main_path):

   if cursor_loc.file is None:
      return False

   try:
      return str(Path(str(cursor_loc.file)).resolve()) == main_path
   except Exception:
      return False


def _collect_decl_lines(tu, file_path):

   if tu is None:
      return set()

   main = _resolve_main(file_path)

   if main is None:
      return set()

   out = set()

   for cursor in tu.cursor.walk_preorder():

      if cursor.kind != CursorKind.DECL_STMT:
         continue

      if not _is_main_file(cursor.location, main):
         continue

      try:

         for ln in range(cursor.extent.start.line,
                         cursor.extent.end.line + 1):
            out.add(ln)

      except Exception:
         pass

   return out


def _collect_signature_lines(tu, file_path):

   if tu is None:
      return set()

   main = _resolve_main(file_path)

   if main is None:
      return set()

   out = set()

   for cursor in tu.cursor.walk_preorder():

      if cursor.kind != CursorKind.FUNCTION_DECL:
         continue

      if not _is_main_file(cursor.location, main):
         continue

      try:
         start = cursor.extent.start.line
      except Exception:
         continue

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


def _collect_continuation_lines(tu, file_path):

   if tu is None:
      return set()

   main = _resolve_main(file_path)

   if main is None:
      return set()

   out = set()

   # Only expression-level cursors whose extent is genuinely one
   # multi-line statement. Control flow (IF_STMT, FOR_STMT, etc.)
   # must NOT be here: their extent covers the entire compound
   # body, which would mark every line inside as 'cont' and starve
   # the harmony check of normal lines.
   STMT_KINDS = {
      CursorKind.CALL_EXPR,
      CursorKind.RETURN_STMT,
      CursorKind.BINARY_OPERATOR,
      CursorKind.COMPOUND_ASSIGNMENT_OPERATOR,
   }

   for cursor in tu.cursor.walk_preorder():

      if cursor.kind not in STMT_KINDS:
         continue

      if not _is_main_file(cursor.location, main):
         continue

      try:
         start = cursor.extent.start.line
         end = cursor.extent.end.line
      except Exception:
         continue

      if end <= start:
         continue

      for ln in range(start + 1, end + 1):
         out.add(ln)

   return out


def _collect_function_bodies(tu, file_path):

   if tu is None:
      return []

   main = _resolve_main(file_path)

   if main is None:
      return []

   bodies = []

   for cursor in tu.cursor.walk_preorder():

      if cursor.kind != CursorKind.FUNCTION_DECL:
         continue

      if not _is_main_file(cursor.location, main):
         continue

      try:
         is_def = cursor.is_definition()
      except Exception:
         continue

      if not is_def:
         continue

      for ch in cursor.get_children():

         if ch.kind == CursorKind.COMPOUND_STMT:
            bodies.append(
               (ch.extent.start.line, ch.extent.end.line)
            )
            break

   return bodies


def _text_find_function_bodies(lines):

   bodies = []
   i = 0

   while i < len(lines):

      if lines[i].rstrip() == '{':

         depth = 1
         start = i + 1
         j = i + 1

         while j < len(lines) and depth > 0:

            for ch in lines[j]:

               if ch == '{':
                  depth += 1
               elif ch == '}':
                  depth -= 1

            j += 1

         bodies.append((start + 1, j))
         i = j

      else:
         i += 1

   return bodies


# ── Pattern detection ────────────────────────────────────────────

def _detect_alternating(normal_entries):
   """Detect alternating long-short-long-short patterns in a
   sequence of (line_no, length) pairs. Returns line numbers
   that are part of such patterns."""

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


def _count_partners(target_len, target_idx, normal_entries):

   count = 0

   for i, (_, length) in enumerate(normal_entries):

      if i == target_idx:
         continue

      if abs(length - target_len) <= PARTNER_RANGE:
         count += 1

   return count


# ── Statistics ───────────────────────────────────────────────────

def _iqr_fence(lengths):

   n = len(lengths)

   if n < 4:
      return 999.0

   s = sorted(lengths)
   q1 = s[n // 4]
   q3 = s[3 * n // 4]
   iqr = max(q3 - q1, MIN_IQR)
   return q3 + IQR_MULTIPLIER * iqr


def _median(lengths):

   s = sorted(lengths)
   return float(s[len(s) // 2])


# ── Rule ─────────────────────────────────────────────────────────

class HarmonyWithNeighbors(Rule):

   id = 'harmony_with_neighbors'
   description = (
      'An under-80-col line that is a statistical outlier (IQR '
      'fence) within its function, not part of a recognized '
      'pattern (alternating pairs, printf calls, string-heavy '
      'content), and has no length partners nearby may indicate '
      'visual disharmony.'
   )
   layers = 'S+R'
   needs_tu = True
   severity = SEVERITY_WARNING
   default_score = SCORE_SOFT

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      masked = _tokens_mod.mask_non_code(ctx.source_text)
      masked_lines = masked.split('\n')
      raw_lengths = [len(line.rstrip()) for line in ctx.lines]

      kinds = [
         _text_classify(
            ctx.lines[i] if i < len(ctx.lines) else '',
            masked_lines[i],
         )
         for i in range(len(masked_lines))
      ]

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

      func_bodies = _collect_function_bodies(ctx.tu, ctx.file_path)

      if not func_bodies:
         func_bodies = _text_find_function_bodies(ctx.lines)

      out = []

      for body_start, body_end in func_bodies:

         # Visual rhythm: all substantial lines (code + comments).
         # Harmony is about what you SEE -- a code line shorter
         # than nearby comment lines is not an outlier.
         visual_lens = []
         normal = []

         for line_no in range(body_start, body_end + 1):

            idx = line_no - 1

            if idx >= len(ctx.lines):
               break

            raw_stripped = ctx.lines[idx].strip()

            if not raw_stripped or raw_stripped in _BRACE_ONLY_LINES:
               continue

            visual_lens.append(raw_lengths[idx])

            if idx < len(kinds) and kinds[idx] == 'normal':
               normal.append((line_no, raw_lengths[idx]))

         if len(normal) < MIN_NORMAL_LINES:
            continue

         if len(visual_lens) < MIN_NORMAL_LINES:
            continue

         fence = _iqr_fence(visual_lens)
         med = _median(visual_lens)
         alternating = _detect_alternating(normal)

         for entry_idx, (line_no, length) in enumerate(normal):

            if length < MIN_TARGET_LEN:
               continue

            if length <= fence:
               continue

            delta = length - med

            if delta < MIN_DELTA_FROM_MEDIAN:
               continue

            idx = line_no - 1
            raw = ctx.lines[idx] if idx < len(ctx.lines) else ''

            if _is_printf_line(raw):
               continue

            if _is_string_heavy(raw):
               continue

            if line_no in alternating:
               continue

            m_stripped = masked_lines[idx].rstrip()

            if m_stripped.endswith(','):
               continue

            if masked_lines[idx].count(',') > MAX_COMMAS:
               continue

            partners = sum(
               1 for vl in visual_lens
               if abs(vl - length) <= PARTNER_RANGE
            ) - 1

            if partners > MAX_PARTNERS:
               continue

            # Continuous cost: scales from COST_MILD at the fence
            # to COST_SIGNIFICANT at 2x overshoot.
            overshoot = (length - fence) / max(fence - med, 1)
            cost = min(
               COST_SIGNIFICANT,
               COST_MILD + (COST_SIGNIFICANT - COST_MILD) * overshoot,
            )

            out.append(Diagnostic(
               file=str(ctx.file_path),
               line=line_no,
               col=1,
               end_line=line_no,
               end_col=length + 1,
               rule=self.id,
               severity=self.severity,
               message=(
                  'line is {} cols; function median {:.0f} '
                  '(+{} delta); IQR fence {:.0f}'
               ).format(length, med, int(delta), fence),
               snippet=raw.rstrip(),
               is_gradient=True,
               prettiness_cost=cost,
            ))

      return out


RULE = HarmonyWithNeighbors()
