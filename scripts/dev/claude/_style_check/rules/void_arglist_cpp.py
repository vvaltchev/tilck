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
   LAYER_RAW_TEXT,
   SEVERITY_WARNING,
   SCORE_SOFT,
)
from .. import tokens as _tokens_mod

# Match `identifier(void)` -- function declarations/definitions.
# Require an identifier (not a keyword) immediately before the paren.
_VOID_ARGLIST_PAT = re.compile(
   r'\b([A-Za-z_]\w*)\s*\(\s*void\s*\)'
)

# Things that look like function decls but are not.
_NON_FN_KEYWORDS = {
   'sizeof', 'typeof', 'alignof', '_Alignof', '__typeof__',
   'return', 'if', 'while', 'for', 'switch', 'case', 'defined',
}


class VoidArglistCpp(Rule):

   id = 'void_arglist_cpp'
   description = (
      'In C++ files, `(void)` in function declarations is '
      'unnecessary; use `()` instead (except inside `extern "C"`)'
   )
   layers = LAYER_RAW_TEXT
   severity = SEVERITY_WARNING
   default_score = SCORE_SOFT
   applies_to = {'.cpp'}

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      if not ctx.is_cpp:
         return []

      # Track extern "C" { ... } nesting.
      extern_c_depth = 0
      in_extern_c = set()  # set of line numbers inside extern "C"

      # First pass: find extern "C" { ... } blocks
      brace_stack = []

      for lineno, line_text in enumerate(ctx.lines, start=1):

         stripped = line_text.strip()

         # Detect `extern "C" {`
         if re.match(r'^extern\s+"C"\s*\{', stripped):
            extern_c_depth += 1
            brace_stack.append(('extern_c', lineno))
            continue

         # Count braces on this line (simplified -- works for typical
         # code where { and } are not inside strings on the same line
         # as extern "C")
         for ch in stripped:

            if ch == '{':
               if extern_c_depth > 0:
                  brace_stack.append(('inner', lineno))
            elif ch == '}':
               if brace_stack:
                  tag, _ = brace_stack.pop()
                  if tag == 'extern_c':
                     extern_c_depth -= 1

         if extern_c_depth > 0:
            in_extern_c.add(lineno)

      # Second pass: find (void) arglists
      masked = _tokens_mod.mask_non_code(ctx.source_text)
      out = []

      for m in _VOID_ARGLIST_PAT.finditer(masked):

         ident = m.group(1)

         if ident in _NON_FN_KEYWORDS:
            continue

         line, col = _tokens_mod.offset_to_line_col(masked, m.start())

         # Skip lines inside extern "C" blocks
         if line in in_extern_c:
            continue

         line_text = ctx.lines[line - 1] \
            if line - 1 < len(ctx.lines) else ''

         # Also skip single-line extern "C" declarations
         if 'extern "C"' in line_text:
            continue

         # Build fix: replace (void) with ()
         fixes = []

         if line_text:
            # Find the `ident(void)` on the line
            pat_str = re.escape(ident) + r'\s*\(\s*void\s*\)'
            line_m = re.search(pat_str, line_text)

            if line_m:
               fixed = (line_text[:line_m.start()]
                        + ident + '()'
                        + line_text[line_m.end():])
               fixes.append(Fix(
                  line, line, [fixed],
                  'replace (void) with ()'
               ))

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=line,
            col=col,
            end_line=line,
            end_col=col + len(m.group(0)),
            rule=self.id,
            severity=self.severity,
            message=(
               '`{}(void)` is unnecessary in C++; '
               'use `{}()` instead'
            ).format(ident, ident),
            snippet=line_text.strip(),
            fixes=fixes,
         ))

      return out


RULE = VoidArglistCpp()
