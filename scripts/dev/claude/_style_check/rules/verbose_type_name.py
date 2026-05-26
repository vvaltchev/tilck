# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring

import re

from typing import List

from .base import (
   Rule,
   Diagnostic,
   Fix,
   CheckContext,
   LAYER_TOKENS,
   SEVERITY_WARNING,
   SCORE_STRONG_PREF,
)
from .. import tokens as _tokens_mod

# Verbose type -> short typedef, ordered longest-first so that
# "unsigned long long" matches before "unsigned long".
_VERBOSE_TYPES = [
   (r'unsigned\s+long\s+long', 'u64'),
   (r'signed\s+long\s+long',   's64'),
   (r'unsigned\s+long',        'ulong'),
   (r'unsigned\s+int',         'u32'),
   (r'unsigned\s+short',       'u16'),
   (r'unsigned\s+char',        'u8'),
]

# Combined pattern: whole-word match, not preceded/followed by
# another identifier character. Longest alternatives first.
_PAT = re.compile(
   r'\b(?:' +
   '|'.join(p for p, _ in _VERBOSE_TYPES) +
   r')\b'
)

# Map from the normalised (single-space) form to the short typedef.
_NORM_MAP = {}
for _pat_str, _short in _VERBOSE_TYPES:
   _norm = _pat_str.replace(r'\s+', ' ')
   _NORM_MAP[_norm] = _short

# Detect #include of any tilck/ header.
_TILCK_INCLUDE = re.compile(r'^\s*#\s*include\s+[<"]tilck/', re.MULTILINE)

# Detect sys_* function signature start (definition or declaration).
_SYS_FUNC = re.compile(r'\bsys_\w+\s*\(')


def _inside_angle_brackets(line, pos):
   """Return True if `pos` (0-based) sits inside `<...>` nesting on
   the same line.  Used to skip C++ template arguments like
   `std::is_same<unsigned long, ...>`."""

   depth = 0

   for i, ch in enumerate(line):

      if i >= pos:
         return depth > 0

      if ch == '<':
         depth += 1
      elif ch == '>':

         if depth > 0:
            depth -= 1

   return False


def _in_sys_signature(lines, match_line_idx):
   """Return True if the match line is part of a sys_* function
   signature (multi-line signatures: scan backward from the match
   line to the opening line, up to 5 lines)."""

   start = max(0, match_line_idx - 5)

   for i in range(match_line_idx, start - 1, -1):
      ln = lines[i]

      if _SYS_FUNC.search(ln):
         return True

      # Stop scanning backward if we hit a line that can't be
      # part of a function signature (empty, or starts with '{').
      stripped = ln.strip()

      if not stripped or stripped == '{':
         return False

   return False


class VerboseTypeName(Rule):

   id = 'verbose_type_name'
   description = (
      'Use project typedefs (u64, ulong, u32, u16, u8, s64) '
      'instead of verbose C type names'
   )
   layers = LAYER_TOKENS
   severity = SEVERITY_WARNING
   default_score = SCORE_STRONG_PREF

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      # Only check files that include a tilck/ header (which means
      # basic_defs.h and its typedefs are transitively available).
      if not _TILCK_INCLUDE.search(ctx.source_text):
         return []

      masked = _tokens_mod.mask_non_code(ctx.source_text)
      out = []

      for m in _PAT.finditer(masked):

         start = m.start()
         line, col = _tokens_mod.offset_to_line_col(masked, start)
         matched_text = m.group(0)

         # Normalise whitespace for lookup.
         norm = re.sub(r'\s+', ' ', matched_text)

         short = _NORM_MAP.get(norm)

         if not short:
            continue

         # Skip sys_* function signatures (Linux ABI compatibility).
         if _in_sys_signature(ctx.lines, line - 1):
            continue

         # In C++ files, skip verbose types inside template angle
         # brackets (e.g. `std::is_same<unsigned long, ...>`).
         line_text = ctx.lines[line - 1] if line <= len(ctx.lines) else ''

         if ctx.is_cpp and _inside_angle_brackets(line_text, col - 1):
            continue

         # Build fix: replace the verbose type with the short typedef.
         fixes = []

         if line_text:
            fixed_line = (line_text[:col - 1] +
                          short +
                          line_text[col - 1 + len(matched_text):])
            fixes.append(Fix(line, line,
                              [fixed_line.rstrip()],
                              "replace '{}' with '{}'".format(
                                 norm, short)))

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=line,
            col=col,
            end_line=line,
            end_col=col + len(matched_text),
            rule=self.id,
            severity=self.severity,
            message=(
               "Use '{}' instead of '{}'".format(short, norm)
            ),
            snippet=line_text.strip(),
            fixes=fixes,
         ))

      return out


RULE = VerboseTypeName()
