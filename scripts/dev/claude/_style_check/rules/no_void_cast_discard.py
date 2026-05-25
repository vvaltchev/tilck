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
   SCORE_MEDIUM_PREF,
)
from .. import tokens as _tokens_mod

# Cast to void: `(void) ident;` — bare identifier, no function call.
# Matches `(void) src;` but NOT `(void) func(args);`.
_PAT = re.compile(r'\(\s*void\s*\)\s*(\w+)\s*;')


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

         ident = m.group(1)
         line, col = _tokens_mod.offset_to_line_col(masked, m.start())
         line_text = ctx.lines[line - 1] \
            if line - 1 < len(ctx.lines) else ''

         # Skip when the identifier is a local variable, not a
         # parameter. Heuristic: scan backward from this line for
         # a declaration or assignment of `ident` within the same
         # function body. If the identifier is assigned to (written),
         # it's a local variable being silenced for release builds.
         # Only bare `(void) param;` with no prior assignment is the
         # unused-parameter anti-pattern.
         is_local = False

         for k in range(line - 2, -1, -1):

            prev = ctx.lines[k].strip() if k < len(ctx.lines) else ''

            if prev == '{':
               # Hit function-body opening brace. If we found no
               # assignment, check if ident appears in the function
               # signature (it's a parameter). Stop scanning.
               break

            # If ident is assigned to anywhere above, it's a local.
            if re.search(r'\b' + re.escape(ident) + r'\s*=', prev):
               is_local = True
               break

            # If ident appears in a declaration (type + ident or
            # type + ..., ident, ...), it's a local.
            if re.search(r'\b' + re.escape(ident) + r'\b', prev):
               if re.search(
                  r'(?:int|long|u32|u64|u16|u8|size_t|ssize_t|bool|'
                  r'char|void|ulong|offt|struct\s+\w+|enum\s+\w+)'
                  r'\s', prev
               ):
                  is_local = True
                  break

         if is_local:
            continue

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=line,
            col=col,
            end_line=line,
            end_col=col + len(m.group(0)),
            rule=self.id,
            severity=self.severity,
            message=(
               '(void){} silences an unused parameter; prefer '
               'naming it `unused` instead'.format(ident)
            ),
            snippet=line_text.strip(),
            is_gradient=True,
            prettiness_cost=COST_MILD,
         ))

      return out


RULE = NoVoidCastDiscard()
