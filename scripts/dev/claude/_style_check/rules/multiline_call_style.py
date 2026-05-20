# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring

from typing import List

from .base import (
   Rule,
   Diagnostic,
   CheckContext,
)
from .. import tokens as _tokens_mod


class MultilineCallStyle(Rule):

   id = 'multiline_call_style'
   description = (
      'Multi-line calls use Style 1 (args aligned to opening paren) or '
      'Style 2 (open paren ends first line, closing ); on its own line); '
      'Style 3 (top-heavy: args indented, ) at end of last arg) is rejected'
   )
   layers = 'S+T'
   needs_tu = True
   applies_to = {'.c'}

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      out = []
      seen = set()

      for ext in ctx.extents:

         if ext.kind != 'CALL_EXPR':
            continue

         start_ofs = _tokens_mod.line_col_to_offset(
            ctx.source_text, ext.start_line, ext.start_col
         )

         if start_ofs < 0:
            continue

         open_paren = ctx.source_text.find('(', start_ofs)

         if open_paren < 0:
            continue

         close_paren = _tokens_mod.find_matching_close(
            ctx.source_text, open_paren
         )

         if close_paren < 0:
            continue

         open_line, open_col = _tokens_mod.offset_to_line_col(
            ctx.source_text, open_paren
         )
         close_line, close_col = _tokens_mod.offset_to_line_col(
            ctx.source_text, close_paren
         )

         if open_line == close_line:
            continue  # single-line call

         # Style 3 is specifically the pattern:
         #     callee(
         #        arg, arg, arg);    <-- ALL args on one indented line, ');' at end
         # i.e. close_line == open_line + 1 with `(` ending the first
         # line and `);` ending the second.
         #
         # When args span MORE than one line (e.g. `printk(...)` with
         # one arg per line, closing `);` on the same line as the last
         # arg), the corpus accepts it as a Style 2 variant.

         if close_line != open_line + 1:
            continue  # not the narrow Style 3 shape

         if open_line - 1 >= len(ctx.lines):
            continue

         open_line_text = ctx.lines[open_line - 1]
         after_open = open_line_text[open_col:]  # text after `(`

         if after_open.strip() != '':
            continue  # Style 1: first arg on same line as `(`

         if close_line - 1 >= len(ctx.lines):
            continue

         close_line_text = ctx.lines[close_line - 1]
         before_close = close_line_text[:close_col - 1]

         if before_close.strip() == '':
            continue  # Style 2: `)` alone on the line

         # Style 3: args on a single indented line, `)` at the end.

         key = (open_line, open_col)

         if key in seen:
            continue

         seen.add(key)

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=close_line,
            col=close_col,
            end_line=close_line,
            end_col=close_col + 1,
            rule=self.id,
            severity=self.severity,
            message=(
               'multi-line call: closing ) must be on its own line '
               '(Style 2), or the first arg must be on the ( line '
               '(Style 1) -- never Style 3 (top-heavy hybrid)'
            ),
            snippet=close_line_text.strip(),
         ))

      return out


RULE = MultilineCallStyle()
