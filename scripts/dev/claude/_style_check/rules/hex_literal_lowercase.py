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

# Match 0x<hex...> where at least one hex digit is uppercase A-F.
# 0X (uppercase prefix) is also flagged.
_PAT_UPPER_X = re.compile(r'\b0X[0-9a-fA-F]+\b')
_PAT_UPPER_DIGIT = re.compile(r'\b0x[0-9a-fA-F]*[A-F][0-9a-fA-F]*\b')


class HexLiteralLowercase(Rule):

   id = 'hex_literal_lowercase'
   description = 'Hex literals: 0xab, not 0xAB or 0XAB'
   layers = LAYER_TOKENS

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      masked = _tokens_mod.mask_non_code(ctx.source_text)
      out = []

      for pat in (_PAT_UPPER_X, _PAT_UPPER_DIGIT):

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
               message='hex literal must use lowercase: ' + m.group(0),
               snippet=line_text.strip(),
            ))

      return out


RULE = HexLiteralLowercase()
