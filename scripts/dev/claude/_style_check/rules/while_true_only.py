# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring

import re

from typing import List

from .base import (
      Rule,
   Diagnostic,
   CheckContext,
   LAYER_TOKENS,
   SEVERITY_WARNING,
   SCORE_STRONG_PREF,
)
from .. import tokens as _tokens_mod

# Q31: infinite loops use `while (true)` only. `while (1)` and
# `for (;;)` are forbidden as hard rules. Matched on the masked
# source to skip strings and comments.
_WHILE_NUM_PAT = re.compile(r'\bwhile\s*\(\s*[1-9][0-9]*[uUlL]*\s*\)')
_FOR_EMPTY_PAT = re.compile(r'\bfor\s*\(\s*;\s*;\s*\)')


class WhileTrueOnly(Rule):

   id = 'while_true_only'
   description = (
      'Infinite loops use `while (true)`; `while (1)` and `for (;;)` '
      'are forbidden'
   )
   layers = LAYER_TOKENS
   severity = SEVERITY_WARNING
   default_score = SCORE_STRONG_PREF

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      masked = _tokens_mod.mask_non_code(ctx.source_text)
      out = []

      for pat, what in (
         (_WHILE_NUM_PAT, 'while-with-numeric-constant'),
         (_FOR_EMPTY_PAT, 'for(;;)'),
      ):

         for m in pat.finditer(masked):

            line, col = _tokens_mod.offset_to_line_col(masked, m.start())
            line_text = ctx.lines[line - 1] \
               if line - 1 < len(ctx.lines) else ''

            out.append(Diagnostic(
               file=str(ctx.file_path),
               line=line,
               col=col,
               end_line=line,
               end_col=col + len(m.group(0)),
               rule=self.id,
               severity=self.severity,
               message=('infinite loop should use `while (true)`; '
                        '`{}` is forbidden').format(what),
               snippet=line_text.strip(),
               suggestion='while (true) { ... }',
            ))

      return out


RULE = WhileTrueOnly()
