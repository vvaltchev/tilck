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


class PerCaseBracesWhenLocals(Rule):

   id = 'per_case_braces_when_locals'
   description = (
      'When a switch case has its own local declarations, the case '
      'body must be braced (introduce a sub-block `{ ... }`). Q12 '
      'hard rule (per-case braces are language-mandatory when '
      'declarations are present at the case body level).'
   )
   layers = 'S+T'
   needs_tu = True
   default_score = SCORE_HARD_RULE

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      if ctx.tu is None:
         return []

      out = []
      seen = set()
      main_file = str(Path(str(ctx.file_path)).resolve())

      for cursor in ctx.tu.cursor.walk_preorder():

         if cursor.kind not in (CursorKind.CASE_STMT,
                                CursorKind.DEFAULT_STMT):
            continue

         loc = cursor.location

         if loc.file is None:
            continue

         try:

            if str(Path(str(loc.file)).resolve()) != main_file:
               continue

         except Exception:
            continue

         # The case's body is its last child (case-value cursor is
         # first for CASE_STMT; DEFAULT_STMT has only the body).
         children = list(cursor.get_children())

         if not children:
            continue

         body = children[-1]

         # If the body is already a COMPOUND_STMT (braced), no
         # violation.
         if body.kind == CursorKind.COMPOUND_STMT:
            continue

         # Body is a single statement. If it itself is a DECL_STMT,
         # the case has an un-braced local declaration -- violation.
         has_decl = False

         if body.kind == CursorKind.DECL_STMT:
            has_decl = True

         else:

            # Walk the body tree for any DECL_STMT (defensive).
            for sub in body.walk_preorder():

               if sub.kind == CursorKind.DECL_STMT:
                  has_decl = True
                  break

         if not has_decl:
            continue

         key = (loc.line, loc.column)

         if key in seen:
            continue

         seen.add(key)

         line_text = ctx.lines[loc.line - 1] \
            if loc.line - 1 < len(ctx.lines) else ''

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=loc.line,
            col=loc.column,
            end_line=loc.line,
            end_col=loc.column + 4,
            rule=self.id,
            severity=self.severity,
            message=('case at line {} has a local declaration in its '
                     'body but is not braced -- wrap the case body in '
                     '`{{ ... }}`').format(loc.line),
            snippet=line_text.strip(),
         ))

      return out


RULE = PerCaseBracesWhenLocals()
