# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=broad-except, consider-using-f-string

import re

from pathlib import Path
from typing import List

import clang.cindex
from clang.cindex import CursorKind

from .base import (
   Rule,
   Diagnostic,
   CheckContext,
   SEVERITY_WARNING,
   SCORE_SOFT,
)
from .. import harmony
from .. import tokens as _tokens_mod
from ..libclang import kind_of

# The statistics (IQR fence, alternation, partners, cost) are the shared
# model in harmony.py. What is C's own lives here: which lines are
# "normal" (not declarations, signatures, continuations, braces, labels,
# preprocessor lines), where function bodies are, and which outliers C
# excuses (printf-style calls, string-heavy lines, initializer rows and
# calls with many arguments).

# ── Thresholds (the rest are the model's, in harmony.py) ─────────

MAX_COMMAS = 4


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

      if kind_of(cursor) != CursorKind.DECL_STMT:
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

      if kind_of(cursor) != CursorKind.FUNCTION_DECL:
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

            if kind_of(ch) == CursorKind.COMPOUND_STMT:
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

      if kind_of(cursor) not in STMT_KINDS:
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

      if kind_of(cursor) != CursorKind.FUNCTION_DECL:
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

         if kind_of(ch) == CursorKind.COMPOUND_STMT:
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

      def exempt(line_no):

         raw = ctx.lines[line_no - 1]
         masked_line = masked_lines[line_no - 1]

         return (_is_printf_line(raw) or
                 _is_string_heavy(raw) or
                 masked_line.rstrip().endswith(',') or
                 masked_line.count(',') > MAX_COMMAS)

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

         for o in harmony.outliers(visual_lens, normal, exempt):

            raw = ctx.lines[o.line - 1]

            out.append(Diagnostic(
               file=str(ctx.file_path),
               line=o.line,
               col=1,
               end_line=o.line,
               end_col=o.length + 1,
               rule=self.id,
               severity=self.severity,
               message=o.message(),
               snippet=raw.rstrip(),
               is_gradient=True,
               prettiness_cost=o.cost,
            ))

      return out


RULE = HarmonyWithNeighbors()
