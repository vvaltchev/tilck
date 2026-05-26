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
   LAYER_RAW_TEXT,
)

# Files exempt from the rule (upstream/external headers preserve their
# original style):
EXEMPT_PATH_FRAGMENTS = (
   '/include/system_headers/',
   '/3rd_party/',
)

# Hard cap on how many lines we'll walk before giving up. Real
# headers always have `#pragma once` (or the legacy guard) within
# a few non-comment lines from the top -- if a header has no
# preprocessor directive in 200 lines, something else is going on
# and the rule isn't useful.
HEAD_SCAN_LINES = 200

_PAT_IFNDEF = re.compile(r'^\s*#\s*ifndef\s+([A-Z_][A-Z0-9_]*_H_*)\s*$')
_PAT_DEFINE = re.compile(r'^\s*#\s*define\s+([A-Z_][A-Z0-9_]*_H_*)\b')


def _first_code_line(lines):
   """Walk lines, skipping blank lines and block / line comments
   (including the SPDX header). Return the 1-based index of the
   FIRST non-comment non-blank line, or None if we run out."""

   in_block = False

   for i, line in enumerate(lines, start=1):

      stripped = line.strip()

      if not stripped:
         continue

      if in_block:

         if '*/' in stripped:
            in_block = False

         continue

      if stripped.startswith('//'):
         continue

      if stripped.startswith('/*'):

         # Single-line block comment? Otherwise enter block mode.
         if '*/' not in stripped[2:]:
            in_block = True

         continue

      # First real code/preprocessor line.
      return i

   return None


class PragmaOnce(Rule):

   id = 'pragma_once'
   description = (
      'Headers must use "#pragma once" (not "#ifndef _X_H_" guards). '
      'Skipped for `.c.h` implementation-include files and for '
      'multi-include headers tagged with '
      '`/* style_check: multi-include */`.'
   )
   layers = LAYER_RAW_TEXT
   applies_to = {'.h', '.hpp'}

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      path_s = str(ctx.file_path)

      if any(frag in path_s for frag in EXEMPT_PATH_FRAGMENTS):
         return []

      # `.c.h` files are implementation-includes (included exactly
      # once by a specific .c file); a `#pragma once` would be
      # noise on them.
      if ctx.is_c_dot_h:
         return []

      # Multi-include / X-macro headers are re-includable by
      # design -- they MUST NOT have `#pragma once`.
      if ctx.is_multi_include:
         return []

      head = ctx.lines[:HEAD_SCAN_LINES]
      first_code = _first_code_line(head)

      if first_code is None:
         # No code/preprocessor in the scan window. The file is
         # probably all-comment (e.g., a placeholder) -- not our
         # concern here.
         return []

      first_line = head[first_code - 1].strip()
      out = []

      # Case 1: first code line IS `#pragma once`. Compliant.
      if first_line == '#pragma once':
         return []

      # Case 2: first code line is `#ifndef X_H_` and is followed by
      # a matching `#define X_H_`. Legacy guard form -- flag and
      # suggest `#pragma once`.
      m = _PAT_IFNDEF.match(first_line)

      if m:

         guard_sym = m.group(1)

         # Look ahead for a matching #define within a few lines.
         for j in range(first_code, min(first_code + 4, len(head))):

            cand = head[j]

            if cand.strip() == '':
               continue

            m2 = _PAT_DEFINE.match(cand)

            if m2 and m2.group(1) == guard_sym:

               out.append(Diagnostic(
                  file=path_s,
                  line=first_code,
                  col=1,
                  end_line=first_code,
                  end_col=len(head[first_code - 1]) + 1,
                  rule=self.id,
                  severity=self.severity,
                  message=(
                     'use "#pragma once" instead of #ifndef {} '
                     'header guards'
                  ).format(guard_sym),
                  snippet=head[first_code - 1].strip(),
               ))
               return out

            break

      # Case 3: first code line is something else (e.g., directly
      # `#include`, a `#define`, or even C code). Missing pragma.
      out.append(Diagnostic(
         file=path_s,
         line=first_code,
         col=1,
         end_line=first_code,
         end_col=len(head[first_code - 1]) + 1,
         rule=self.id,
         severity=self.severity,
         message=(
            'missing "#pragma once" -- the first non-comment line '
            'of a header must be `#pragma once`'
         ),
         snippet=head[first_code - 1].strip(),
      ))

      return out


RULE = PragmaOnce()
