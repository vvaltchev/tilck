# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring

import re

from typing import List

from .base import (
   Rule,
   Diagnostic,
   Fix,
   CheckContext,
   LAYER_TOKENS,
   SEVERITY_WARNING,
   SCORE_SOFT,
)
from .. import tokens as _tokens_mod

_PAT_UPPER_X = re.compile(r'\b0X[0-9a-fA-F]+\b')
_PAT_UPPER_DIGIT = re.compile(r'\b0x[0-9a-fA-F]*[A-F][0-9a-fA-F]*\b')


class HexLiteralLowercase(Rule):

   id = 'hex_literal_lowercase'
   description = (
      'Hex literals: prefer 0xab over 0xAB or 0XAB. Soft preference '
      '(small prettiness penalty); not a hard rule -- existing '
      'uppercase code is not a defect.'
   )
   layers = LAYER_TOKENS
   severity = SEVERITY_WARNING
   default_score = SCORE_SOFT

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      masked = _tokens_mod.mask_non_code(ctx.source_text)
      out = []

      for pat in (_PAT_UPPER_X, _PAT_UPPER_DIGIT):

         for m in pat.finditer(masked):

            line, col = _tokens_mod.offset_to_line_col(masked, m.start())
            line_text = ctx.lines[line - 1] \
               if line - 1 < len(ctx.lines) else ''

            start = col - 1
            end = start + len(m.group(0))
            lowered = m.group(0).lower()
            fixed_line = line_text[:start] + lowered + line_text[end:]

            out.append(Diagnostic(
               file=str(ctx.file_path),
               line=line,
               col=col,
               end_line=line,
               end_col=col + len(m.group(0)),
               rule=self.id,
               severity=self.severity,
               message='hex literal must use lowercase: ' + m.group(0),
               snippet=line_text.strip(),
               fixes=[Fix(line, line, [fixed_line])],
            ))

      return out


RULE = HexLiteralLowercase()
