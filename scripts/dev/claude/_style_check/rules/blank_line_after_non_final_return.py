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


class BlankLineAfterNonFinalReturn(Rule):

   id = 'blank_line_after_non_final_return'
   description = (
      'Blank line required after every `return` statement except the '
      'last in its enclosing block (Q18)'
   )
   layers = 'S+R'
   needs_tu = True
   default_score = SCORE_HARD_RULE

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      if ctx.tu is None:
         return []

      out = []
      seen = set()
      main_file = str(Path(str(ctx.file_path)).resolve())

      for cursor in ctx.tu.cursor.walk_preorder():

         if cursor.kind != CursorKind.COMPOUND_STMT:
            continue

         loc = cursor.location

         if loc.file is None:
            continue

         try:

            if str(Path(str(loc.file)).resolve()) != main_file:
               continue

         except Exception:
            continue

         children = list(cursor.get_children())

         for i, child in enumerate(children):

            if child.kind != CursorKind.RETURN_STMT:
               continue

            if i == len(children) - 1:
               continue  # last statement in the block -- no blank needed

            return_end_line = child.extent.end.line

            if return_end_line >= len(ctx.lines):
               continue

            # The line immediately AFTER the return statement is
            # ctx.lines[return_end_line] (0-indexed -> 1-based
            # return_end_line + 1).
            next_line = ctx.lines[return_end_line]

            if next_line.strip() == '':
               continue  # blank line present -- compliant

            key = (return_end_line,)

            if key in seen:
               continue

            seen.add(key)

            ret_line_text = ctx.lines[return_end_line - 1] \
               if return_end_line - 1 < len(ctx.lines) else ''

            out.append(Diagnostic(
               file=str(ctx.file_path),
               line=return_end_line + 1,
               col=1,
               end_line=return_end_line + 1,
               end_col=len(next_line) + 1,
               rule=self.id,
               severity=self.severity,
               message=('blank line required after non-final `return` '
                        'statement on line {}').format(return_end_line),
               snippet=ret_line_text.strip(),
            ))

      return out


RULE = BlankLineAfterNonFinalReturn()
