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


class BlankLineAfterDeclBlock(Rule):

   id = 'blank_line_after_decl_block'
   description = (
      'Blank line required after the declaration block at the top '
      'of a function or sub-block (Q18)'
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

         if len(children) < 2:
            continue  # not enough children for a decl-then-code pattern

         # Only the LEADING run of DECL_STMTs counts as "the
         # declaration block." A decl that appears mid-block (after a
         # non-declaration statement) is a separate concern handled by
         # non_const_locals_top_of_block, NOT this rule.
         leading_decl_count = 0

         for ch in children:

            if ch.kind == CursorKind.DECL_STMT:
               leading_decl_count += 1
            else:
               break

         if leading_decl_count == 0:
            continue  # no decl block at the start of this scope

         if leading_decl_count == len(children):
            continue  # all decls, no non-decl to follow

         last_decl = children[leading_decl_count - 1]
         first_non_decl = children[leading_decl_count]

         last_decl_line = last_decl.extent.end.line
         first_non_decl_line = first_non_decl.location.line

         # We expect a blank line between them. That means
         # first_non_decl_line >= last_decl_line + 2.

         if first_non_decl_line >= last_decl_line + 2:
            continue  # at least one blank line present

         key = (last_decl_line, first_non_decl_line)

         if key in seen:
            continue

         seen.add(key)

         line_text = ctx.lines[first_non_decl_line - 1] \
            if first_non_decl_line - 1 < len(ctx.lines) else ''

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=first_non_decl_line,
            col=1,
            end_line=first_non_decl_line,
            end_col=len(line_text) + 1,
            rule=self.id,
            severity=self.severity,
            message=('blank line required between the declaration '
                     'block (last decl at line {}) and the first '
                     'non-declaration statement').format(last_decl_line),
            snippet=line_text.strip(),
         ))

      return out


RULE = BlankLineAfterDeclBlock()
