# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=consider-using-f-string

import re

from typing import List

from .base import (
   Rule,
   Diagnostic,
   Fix,
   CheckContext,
   LAYER_TOKENS,
   SEVERITY_WARNING,
   SCORE_MEDIUM_PREF,
)
from .. import tokens as _tokens_mod

# Q11: kfree/kfree2/vfree2/free are no-ops on NULL, so a guard
# `if (ptr) kfree(ptr);` (or `!= NULL`) is unnecessary.
# Only flag functions KNOWN to accept NULL -- not every function
# with "free"/"release" in its name does.

_NULL_SAFE = (
   r'kfree|kfree2|kfree_obj|kfree_array_obj|'
   r'vfree2|kvfree|aligned_kfree2|free'
)

_PAT_GUARD = re.compile(
   r'\bif\s*\(\s*'
   r'(?P<v>[A-Za-z_]\w*)'
   r'(?:\s*!=\s*NULL)?'
   r'\s*\)\s*'
   r'(?:\n\s*)?'
   r'(?P<fn>' + _NULL_SAFE + r')'
   r'\s*\(\s*'
   r'(?P=v)'
   r'(?:\s*,\s*[^)]+)?'     # optional second arg (kfree2 size)
   r'\s*\)\s*;'
)


class FreeNoNullGuard(Rule):

   id = 'free_no_null_guard'
   description = (
      'free_X(NULL) is a no-op in this codebase, so the `if (ptr) '
      'free_X(ptr);` guard is unnecessary and signals a misread '
      'free-contract. Drop the guard. (Q11 soft preference.)'
   )
   layers = LAYER_TOKENS
   severity = SEVERITY_WARNING
   default_score = SCORE_MEDIUM_PREF

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      masked = _tokens_mod.mask_non_code(ctx.source_text)
      out = []

      for m in _PAT_GUARD.finditer(masked):

         line, col = _tokens_mod.offset_to_line_col(masked, m.start())
         line_text = ctx.lines[line - 1] \
            if line - 1 < len(ctx.lines) else ''

         fn = m.group('fn')
         var = m.group('v')

         # Build fix: replace the if+body range with just the free call.
         # The match may span 1 or 2 lines. Detect the line range from
         # the matched text.
         end_line, _ = _tokens_mod.offset_to_line_col(
            masked, m.end() - 1
         )

         # Determine indentation from the `if` line
         if_indent = len(line_text) - len(line_text.lstrip())
         indent_str = line_text[:if_indent]

         # Reconstruct the free call with its full args from source
         # (the regex captured only the variable, but the full call
         # including optional second arg is in the match).
         free_start = m.start('fn')
         free_part = masked[free_start:m.end()].strip()
         fixed_line = indent_str + free_part

         fixes = [Fix(line, end_line, [fixed_line],
                       'remove null guard around free call')]

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=line,
            col=col,
            end_line=end_line,
            end_col=col + len(m.group(0)),
            rule=self.id,
            severity=self.severity,
            message=(
               'guarded `{}({})` -- this codebase\'s free-style '
               'functions accept NULL; drop the `if (...)` wrapper'
            ).format(fn, var),
            snippet=line_text.strip(),
            suggestion='{}({});'.format(fn, var),
            fixes=fixes,
         ))

      return out


RULE = FreeNoNullGuard()
