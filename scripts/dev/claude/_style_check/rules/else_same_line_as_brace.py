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

# `} else` / `} else if` on the SAME line as the closing brace.
# A violation looks like: '}' followed by newline + whitespace + 'else'.
_PAT = re.compile(r'\}\s*\n\s*else\b')


class ElseSameLineAsBrace(Rule):

   id = 'else_same_line_as_brace'
   description = '"} else" and "} else if" go on the same line as the closing brace'
   layers = LAYER_TOKENS

   severity = SEVERITY_WARNING

   default_score = SCORE_STRONG_PREF


   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      masked = _tokens_mod.mask_non_code(ctx.source_text)
      out = []

      for m in _PAT.finditer(masked):

         # Diagnostic anchors at the 'else' token, not the brace.
         else_ofs = masked.find('else', m.start())
         line, col = _tokens_mod.offset_to_line_col(masked, else_ofs)
         line_text = ctx.lines[line - 1] \
            if line - 1 < len(ctx.lines) else ''

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=line,
            col=col,
            end_line=line,
            end_col=col + len('else'),
            rule=self.id,
            severity=self.severity,
            message='"else" must be on the same line as the closing brace',
            snippet=line_text.strip(),
         ))

      return out


RULE = ElseSameLineAsBrace()
