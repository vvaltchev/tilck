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
   LAYER_TOKENS,
   SEVERITY_WARNING,
   SCORE_MEDIUM_PREF,
)
from .. import tokens as _tokens_mod

# Q11: free functions in this codebase are no-ops on NULL, so a
# guard `if (ptr) free_X(ptr);` (or `!= NULL`) is an idiom error.
# Detect:
#   if (<ID>)     free_<...>(<ID>);
#   if (<ID> != NULL)  free_<...>(<ID>);
# All on one or two lines.

_PAT_GUARD = re.compile(
   r'\bif\s*\(\s*'
   r'(?P<v>[A-Za-z_]\w*)'
   r'(?:\s*!=\s*NULL)?'
   r'\s*\)\s*'
   r'(?:\n\s*)?'
   r'(?P<fn>(?:k?free|kvfree|destroy|release|put|drop|unref)_\w+)'
   r'\s*\(\s*'
   r'(?P=v)'
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

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=line,
            col=col,
            end_line=line,
            end_col=col + len(m.group(0)),
            rule=self.id,
            severity=self.severity,
            message=(
               'guarded `{}({})` -- this codebase\'s free-style '
               'functions accept NULL; drop the `if (...)` wrapper'
            ).format(fn, var),
            snippet=line_text.strip(),
            suggestion='{}({});'.format(fn, var),
         ))

      return out


RULE = FreeNoNullGuard()
