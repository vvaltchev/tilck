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
   SCORE_STRONG_PREF,
)
from .. import tokens as _tokens_mod

# Q22 hard rule: asymmetric cast form is forbidden.
#
# Default cast spacing:      `(Type *)expr`  -- space inside, no
#                                              space after `)`.
# Acceptable (less preferred): `(Type *) expr` -- both spaces.
# Compact form (line-fit hack): `(Type*)expr` -- no spaces, with
#                                              symmetry constraint.
# FORBIDDEN (asymmetric):    `(Type*) expr`  -- no space inside,
#                                              space after `)`.
#
# Detection: any cast that has `Type*)` (no space before `)`)
# immediately followed by a space and an identifier/expression
# starter is the forbidden asymmetric form. Mask comments/strings
# first to avoid false positives.
_PAT = re.compile(r'\([A-Za-z_][\w\s]*\w\*\)\s+[A-Za-z_(!&*\-]')


class CastNoAsymmetricForm(Rule):

   id = 'cast_no_asymmetric_form'
   description = (
      'Asymmetric cast form `(Type*) expr` is forbidden -- no space '
      'inside the cast but a space after `)`. Use `(Type *)expr` '
      '(default) or `(Type*)expr` (compact form for line-fit only).'
   )
   layers = LAYER_TOKENS
   severity = SEVERITY_WARNING
   default_score = SCORE_STRONG_PREF

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      masked = _tokens_mod.mask_non_code(ctx.source_text)
      out = []

      for m in _PAT.finditer(masked):

         line, col = _tokens_mod.offset_to_line_col(masked, m.start())
         line_text = ctx.lines[line - 1] \
            if line - 1 < len(ctx.lines) else ''

         start = col - 1
         end = start + len(m.group(0))
         matched = line_text[start:end]
         close_idx = matched.index(')')
         cast_inside = matched[:close_idx]
         after_close = matched[close_idx + 1:]
         expr_char = after_close.lstrip()

         star_idx = cast_inside.rfind('*')
         spaced_cast = cast_inside[:star_idx] + ' *'
         fix_default = line_text[:start] + spaced_cast + ')' + \
            expr_char + line_text[end:]
         fix_both = line_text[:start] + spaced_cast + ')' + \
            after_close + line_text[end:]

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=line,
            col=col,
            end_line=line,
            end_col=col + len(m.group(0)),
            rule=self.id,
            severity=self.severity,
            message=('asymmetric cast: no space before `*)` but space '
                     'after `)` -- use `(Type *)expr` (preferred) or '
                     '`(Type *) expr` (also acceptable)'),
            snippet=line_text.strip(),
            fixes=[
               Fix(line, line, [fix_default], '(Type *)expr'),
               Fix(line, line, [fix_both], '(Type *) expr'),
            ],
         ))

      return out


RULE = CastNoAsymmetricForm()
