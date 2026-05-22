# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=broad-except, consider-using-f-string

from pathlib import Path
from typing import List

import clang.cindex
from clang.cindex import CursorKind

from .base import (
   Rule,
   Diagnostic,
   CheckContext,
   SCORE_HARD_RULE,
)
from .. import tokens as _tokens_mod


class FunctionDefNoStyle2(Rule):

   id = 'function_def_no_style2'
   description = (
      'Function definitions cannot use Style 2 (open paren ending the '
      'name line with args indented +3 below). Style 1 (args aligned '
      'under opening paren) is the only acceptable wrapped form '
      'for function definitions (Q4 hard rule).'
   )
   layers = 'S+T'
   needs_tu = True
   applies_to = {'.c'}
   default_score = SCORE_HARD_RULE

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      if ctx.tu is None:
         return []

      out = []
      seen = set()
      main_file = str(Path(str(ctx.file_path)).resolve())

      for cursor in ctx.tu.cursor.walk_preorder():

         if cursor.kind != CursorKind.FUNCTION_DECL:
            continue

         try:

            if not cursor.is_definition():
               continue

         except Exception:
            continue

         loc = cursor.location

         if loc.file is None:
            continue

         try:

            if str(Path(str(loc.file)).resolve()) != main_file:
               continue

         except Exception:
            continue

         name_ofs = _tokens_mod.line_col_to_offset(
            ctx.source_text, loc.line, loc.column
         )

         if name_ofs < 0:
            continue

         open_paren = ctx.source_text.find('(', name_ofs)

         if open_paren < 0:
            continue

         open_line, open_col = _tokens_mod.offset_to_line_col(
            ctx.source_text, open_paren
         )

         close_paren = _tokens_mod.find_matching_close(
            ctx.source_text, open_paren
         )

         if close_paren < 0:
            continue

         close_line, _close_col = _tokens_mod.offset_to_line_col(
            ctx.source_text, close_paren
         )

         if close_line == open_line:
            continue  # single-line signature -- not the wrapped case

         if open_line - 1 >= len(ctx.lines):
            continue

         open_line_text = ctx.lines[open_line - 1]
         after_open = open_line_text[open_col:]

         if after_open.strip() != '':
            continue  # Style 1: first arg on same line as `(`. OK.

         # `(` ends the name line and args are below -> Style 2 shape.
         # Forbidden for function definitions per Q4.

         key = (open_line, open_col, cursor.spelling)

         if key in seen:
            continue

         seen.add(key)

         line_text = ctx.lines[open_line - 1]

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=open_line,
            col=open_col,
            end_line=open_line,
            end_col=open_col + 1,
            rule=self.id,
            severity=self.severity,
            message=('function definition "{}" uses Style 2 (open paren '
                     'ending the name line); Style 2 is forbidden for '
                     'function definitions -- use Style 1 (args aligned '
                     'under the opening paren)').format(cursor.spelling),
            snippet=line_text.strip(),
         ))

      return out


RULE = FunctionDefNoStyle2()
