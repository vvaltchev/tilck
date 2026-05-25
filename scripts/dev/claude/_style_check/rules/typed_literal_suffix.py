# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=consider-using-f-string

import re

from typing import List

from .base import (
   COST_MINOR,
   Rule,
   Diagnostic,
   CheckContext,
   LAYER_TOKENS,
   SEVERITY_WARNING,
   SCORE_NUDGE,
)
from .. import tokens as _tokens_mod

# Q47: for a typed numeric constant, prefer the literal-with-suffix
# form `0x1000u` over the cast form `(u32)0x1000`. Tiny nudge --
# both forms work, but the suffix carries the type with the value.
# Detect `(uN)<num>` and `(sN)<num>` patterns where the cast type
# is a project numeric typedef (u8, u16, u32, u64, s8, ...).
_PAT = re.compile(
   r'\((u8|u16|u32|u64|s8|s16|s32|s64|ulong|slong)\)\s*'
   r'(0x[0-9a-fA-F]+|0[0-7]*|\d+)'
)


class TypedLiteralSuffix(Rule):

   id = 'typed_literal_suffix'
   description = (
      'Prefer typed-literal suffix (`0x1000u`) over a cast '
      '(`(u32)0x1000`) for typed numeric constants -- the suffix '
      'carries the type with the value. (Q47 nudge.)'
   )
   layers = LAYER_TOKENS
   severity = SEVERITY_WARNING
   default_score = SCORE_NUDGE

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      masked = _tokens_mod.mask_non_code(ctx.source_text)
      out = []

      for m in _PAT.finditer(masked):

         line, col = _tokens_mod.offset_to_line_col(masked, m.start())
         line_text = ctx.lines[line - 1] \
            if line - 1 < len(ctx.lines) else ''

         type_name = m.group(1)
         num = m.group(2)

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=line,
            col=col,
            end_line=line,
            end_col=col + len(m.group(0)),
            rule=self.id,
            severity=self.severity,
            message=(
               'cast `({}){}` of a literal -- consider a typed '
               'literal suffix instead'
            ).format(type_name, num),
            snippet=line_text.strip(),
            suggestion='use a literal suffix (e.g. `0x...u`, `1ull`)',
            is_gradient=True,
            prettiness_cost=COST_MINOR,
         ))

      return out


RULE = TypedLiteralSuffix()
