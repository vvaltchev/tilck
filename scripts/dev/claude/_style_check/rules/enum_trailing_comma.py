# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=consider-using-f-string

from typing import List

from .base import (
   Rule,
   Diagnostic,
   Fix,
   CheckContext,
   SEVERITY_WARNING,
   SCORE_SOFT,
)
from .. import tokens as _tokens_mod


class EnumTrailingComma(Rule):

   id = 'enum_trailing_comma'
   description = (
      'Multi-line enums (one value per line) REQUIRE a trailing '
      'comma after the last enumerator. Single-line enums '
      '(all values on one line) must NOT have one.'
   )
   layers = 'S+T'
   needs_tu = True
   applies_to = {'.c', '.h', '.cpp', '.hpp'}
   severity = SEVERITY_WARNING
   default_score = SCORE_SOFT

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      out = []
      seen = set()

      for ext in ctx.extents:

         if ext.kind != 'ENUM_DECL':
            continue

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

         open_line, _ = _tokens_mod.offset_to_line_col(
            ctx.source_text, brace_open
         )
         close_line, _ = _tokens_mod.offset_to_line_col(
            ctx.source_text, brace_close
         )

         body_masked = _tokens_mod.mask_non_code(
            ctx.source_text[brace_open + 1:brace_close]
         )
         rstripped = body_masked.rstrip()

         if not rstripped:
            continue

         last_char = rstripped[-1]
         multi_line = (close_line > open_line + 1)
         has_comma = (last_char == ',')

         if multi_line and has_comma:
            continue

         if not multi_line and not has_comma:
            continue

         last_ofs = brace_open + 1 + len(rstripped) - 1
         line, col = _tokens_mod.offset_to_line_col(
            ctx.source_text, last_ofs
         )

         key = (line, col)

         if key in seen:
            continue

         seen.add(key)

         line_text = ctx.lines[line - 1] \
            if line - 1 < len(ctx.lines) else ''

         if multi_line and not has_comma:

            fixed = line_text[:col] + ',' + line_text[col:]

            out.append(Diagnostic(
               file=str(ctx.file_path),
               line=line,
               col=col,
               end_line=line,
               end_col=col + 1,
               rule=self.id,
               severity=self.severity,
               message=(
                  'multi-line enum: add trailing comma after '
                  'the last enumerator'
               ),
               snippet=line_text.strip(),
               fixes=[Fix(line, line, [fixed])],
            ))

         elif not multi_line and has_comma:

            fixed = line_text[:col - 1] + line_text[col:]

            out.append(Diagnostic(
               file=str(ctx.file_path),
               line=line,
               col=col,
               end_line=line,
               end_col=col + 1,
               rule=self.id,
               severity=self.severity,
               message=(
                  'single-line enum: remove trailing comma '
                  'after the last enumerator'
               ),
               snippet=line_text.strip(),
               fixes=[Fix(line, line, [fixed])],
            ))

      return out


RULE = EnumTrailingComma()
