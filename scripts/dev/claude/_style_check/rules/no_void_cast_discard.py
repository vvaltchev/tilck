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
   SCORE_MEDIUM_PREF,
)
from .. import tokens as _tokens_mod

# Cast to void: `(void)<expression>` where expression starts with an
# identifier or `(` or unary operator. The `void` keyword in function
# args (`void foo(void)`) is always followed by `)` or `,` so the
# trailing-class character distinguishes the cast.
_PAT = re.compile(r'\(\s*void\s*\)[ \t]*([A-Za-z_(!&*])')


class NoVoidCastDiscard(Rule):

   id = 'no_void_cast_discard'
   description = 'No (void)expr casts; kernel does not use this pattern'
   layers = LAYER_TOKENS

   severity = SEVERITY_WARNING

   default_score = SCORE_MEDIUM_PREF


   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      masked = _tokens_mod.mask_non_code(ctx.source_text)
      out = []

      for m in _PAT.finditer(masked):

         line, col = _tokens_mod.offset_to_line_col(masked, m.start())
         line_text = ctx.lines[line - 1] \
            if line - 1 < len(ctx.lines) else ''

         # Only flag standalone `(void)ident;` statements used to
         # silence unused-parameter warnings. Skip when the cast
         # shares a line with other code (e.g. `ASSERT(x); (void)x;`)
         # — those silence unused-variable warnings for locals and
         # are legitimate.
         stripped = line_text.strip()

         if not stripped.startswith('(void)'):
            continue

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=line,
            col=col,
            end_line=line,
            end_col=col + len(m.group(0)),
            rule=self.id,
            severity=self.severity,
            message='no (void)expr discards; use named "unused" params instead',
            snippet=stripped,
         ))

      return out


RULE = NoVoidCastDiscard()
