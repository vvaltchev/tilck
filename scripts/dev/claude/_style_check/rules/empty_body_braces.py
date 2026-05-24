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
   SEVERITY_WARNING,
   SCORE_STRONG_PREF,
)


class EmptyBodyBraces(Rule):

   id = 'empty_body_braces'
   description = (
      'Empty loop body must be `{ }`, never a bare `;`. Applies to '
      '`while (cond);` and `for (...);` polling loops. Q44 hard '
      'rule.'
   )
   layers = 'S+T'
   needs_tu = True
   severity = SEVERITY_WARNING
   default_score = SCORE_STRONG_PREF

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      if ctx.tu is None:
         return []

      out = []
      seen = set()
      main_file = str(Path(str(ctx.file_path)).resolve())

      for cursor in ctx.tu.cursor.walk_preorder():

         if cursor.kind not in (CursorKind.WHILE_STMT,
                                CursorKind.FOR_STMT):
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

         if not children:
            continue

         body = children[-1]

         if body.kind != CursorKind.NULL_STMT:
            continue   # body is not a bare `;`

         key = (loc.line, loc.column)

         if key in seen:
            continue

         seen.add(key)

         line_text = ctx.lines[loc.line - 1] \
            if loc.line - 1 < len(ctx.lines) else ''

         kw = 'while' if cursor.kind == CursorKind.WHILE_STMT else 'for'

         # Guard against macro-expanded loops: if the raw source at
         # the cursor location doesn't start with `while` / `for`,
         # the loop was introduced by a macro and should be skipped.
         raw_at = line_text.lstrip()

         if not raw_at.startswith(kw):
            continue

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=loc.line,
            col=loc.column,
            end_line=loc.line,
            end_col=loc.column + len(kw),
            rule=self.id,
            severity=self.severity,
            message=('`{}` loop with bare `;` body -- use `{{ }}` for '
                     'empty loop body').format(kw),
            snippet=line_text.strip(),
            suggestion='{} (...) {{ }}'.format(kw),
         ))

      return out


RULE = EmptyBodyBraces()
