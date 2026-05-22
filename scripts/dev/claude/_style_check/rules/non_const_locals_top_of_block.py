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


class NonConstLocalsTopOfBlock(Rule):

   id = 'non_const_locals_top_of_block'
   description = (
      'Non-const locals must be declared at the top of their enclosing '
      'scope (function body, control-flow body, or sub-block); C99 mid-'
      'block declarations are forbidden (Q15)'
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

         # The sub-block-scope-narrowing exception is automatic: a
         # sub-block has its own COMPOUND_STMT, where the local sits
         # at the top of that nested scope and does not trigger this
         # rule.
         saw_non_decl = False

         for child in cursor.get_children():

            if child.kind == CursorKind.DECL_STMT:

               if not saw_non_decl:
                  continue

               # A DECL_STMT after a non-declaration statement: check
               # whether any VAR_DECL inside it is non-const.
               for sub in child.get_children():

                  if sub.kind != CursorKind.VAR_DECL:
                     continue

                  try:

                     if sub.type.is_const_qualified():
                        continue

                  except Exception:
                     pass

                  sub_loc = sub.location

                  if sub_loc.file is None:
                     continue

                  try:

                     if str(Path(str(sub_loc.file)).resolve()) != main_file:
                        continue

                  except Exception:
                     continue

                  key = (sub_loc.line, sub_loc.column, sub.spelling)

                  if key in seen:
                     continue

                  seen.add(key)

                  line_text = ctx.lines[sub_loc.line - 1] \
                     if sub_loc.line - 1 < len(ctx.lines) else ''

                  out.append(Diagnostic(
                     file=str(ctx.file_path),
                     line=sub_loc.line,
                     col=sub_loc.column,
                     end_line=sub_loc.line,
                     end_col=sub_loc.column + len(sub.spelling),
                     rule=self.id,
                     severity=self.severity,
                     message=('non-const local "{}" declared after non-'
                              'declaration statements; move to top of '
                              'enclosing scope (function body, control '
                              'body, or a {{ }} sub-block)').format(
                        sub.spelling
                     ),
                     snippet=line_text.strip(),
                  ))

               continue

            # Anything else is a non-declaration statement.
            saw_non_decl = True

      return out


RULE = NonConstLocalsTopOfBlock()
