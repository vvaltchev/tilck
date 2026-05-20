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
)
from .. import tokens as _tokens_mod

# Match "sizeof" as a whole word, optional whitespace, then anything
# that is NOT '(' -- that's the violation. The masked source already
# excludes comment / string content so we won't false-positive on
# /* sizeof foo */ or "sizeof foo".
_PAT = re.compile(r'\bsizeof\b([ \t]*)([^( \t\n])')


class SizeofParens(Rule):

   id = 'sizeof_parens'
   description = (
      'sizeof requires parens: write sizeof(X), not sizeof X'
   )
   layers = LAYER_TOKENS

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      masked = _tokens_mod.mask_non_code(ctx.source_text)

      out = []

      for m in _PAT.finditer(masked):

         start = m.start()
         line, col = _tokens_mod.offset_to_line_col(masked, start)

         line_text = ctx.lines[line - 1] if line <= len(ctx.lines) else ''

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=line,
            col=col,
            end_line=line,
            end_col=col + len('sizeof'),
            rule=self.id,
            severity=self.severity,
            message='sizeof requires parens: write sizeof(X), not sizeof X',
            snippet=line_text.strip(),
         ))

      return out


RULE = SizeofParens()
