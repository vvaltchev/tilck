# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=consider-using-f-string

from typing import List

from .base import (
   Rule,
   Diagnostic,
   CheckContext,
)
from .. import tokens as _tokens_mod


class NoTrailingEnumComma(Rule):

   id = 'no_trailing_enum_comma'
   description = 'No trailing comma after the last enumerator'
   layers = 'S+T'
   needs_tu = True
   applies_to = {'.c', '.h'}

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      out = []
      seen = set()

      for ext in ctx.extents:

         if ext.kind != 'ENUM_DECL':
            continue

         # Find '{' opening the enum body, then the matching '}'.
         start_ofs = _tokens_mod.line_col_to_offset(
            ctx.source_text, ext.start_line, ext.start_col
         )

         if start_ofs < 0:
            continue

         brace_open = ctx.source_text.find('{', start_ofs)

         if brace_open < 0:
            continue

         brace_close = _tokens_mod.find_matching_close(
            ctx.source_text, brace_open, '{', '}'
         )

         if brace_close < 0:
            continue

         # Look backwards from '}' for the last non-whitespace,
         # non-comment char. Easier to walk a masked copy of the body.
         body_masked = _tokens_mod.mask_non_code(
            ctx.source_text[brace_open + 1:brace_close]
         )
         rstripped = body_masked.rstrip()

         if not rstripped:
            continue

         last = rstripped[-1]

         if last != ',':
            continue

         # Compute line/col of the trailing comma
         comma_ofs = brace_open + 1 + len(rstripped) - 1
         line, col = _tokens_mod.offset_to_line_col(
            ctx.source_text, comma_ofs
         )

         key = (line, col)

         if key in seen:
            continue

         seen.add(key)

         line_text = ctx.lines[line - 1] \
            if line - 1 < len(ctx.lines) else ''

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=line,
            col=col,
            end_line=line,
            end_col=col + 1,
            rule=self.id,
            severity=self.severity,
            message='no trailing comma after the last enumerator',
            snippet=line_text.strip(),
         ))

      return out


RULE = NoTrailingEnumComma()
