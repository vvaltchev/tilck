# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring

import re

from typing import List

from .base import (
   COST_MILD,
   Rule,
   Diagnostic,
   CheckContext,
   LAYER_TOKENS,
   SEVERITY_WARNING,
   SCORE_NUDGE,
)
from .. import tokens as _tokens_mod

# Match bare `NULL` as a whole word on non-preprocessor lines.
_NULL_PAT = re.compile(r'\bNULL\b')


class PreferNullptr(Rule):

   id = 'prefer_nullptr'
   description = (
      'Prefer `nullptr` over `NULL` in C++ files'
   )
   layers = LAYER_TOKENS
   severity = SEVERITY_WARNING
   default_score = SCORE_NUDGE
   applies_to = {'.cpp', '.hpp'}

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      if not ctx.is_cpp:
         return []

      masked = _tokens_mod.mask_non_code(ctx.source_text)
      out = []

      for m in _NULL_PAT.finditer(masked):

         line, col = _tokens_mod.offset_to_line_col(masked, m.start())
         line_text = ctx.lines[line - 1] \
            if line - 1 < len(ctx.lines) else ''

         # Skip #define and #include lines
         stripped = line_text.lstrip()
         if stripped.startswith('#'):
            continue

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=line,
            col=col,
            end_line=line,
            end_col=col + 4,
            rule=self.id,
            severity=self.severity,
            message='prefer `nullptr` over `NULL` in C++ code',
            snippet=line_text.strip(),
            suggestion='replace `NULL` with `nullptr`',
            is_gradient=True,
            prettiness_cost=COST_MILD,
         ))

      return out


RULE = PreferNullptr()
