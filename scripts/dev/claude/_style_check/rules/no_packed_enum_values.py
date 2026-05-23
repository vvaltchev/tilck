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


class NoPackedEnumValues(Rule):

   id = 'no_packed_enum_values'
   description = (
      'Enum values must be one per line. Packed form `{ A, B, C, D }` '
      'on one line is forbidden (Q23 hard rule).'
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

         if cursor.kind != CursorKind.ENUM_DECL:
            continue

         loc = cursor.location

         if loc.file is None:
            continue

         try:

            if str(Path(str(loc.file)).resolve()) != main_file:
               continue

         except Exception:
            continue

         children = [
            c for c in cursor.get_children()
            if c.kind == CursorKind.ENUM_CONSTANT_DECL
         ]

         if len(children) < 2:
            continue

         # Walk pairs; flag any pair on the same line.
         last_line = None

         for c in children:

            c_loc = c.location

            if c_loc.file is None:
               continue

            try:

               if str(Path(str(c_loc.file)).resolve()) != main_file:
                  continue

            except Exception:
               continue

            if last_line is not None and c_loc.line == last_line:

               key = (c_loc.line, c.spelling)

               if key not in seen:
                  seen.add(key)

                  line_text = ctx.lines[c_loc.line - 1] \
                     if c_loc.line - 1 < len(ctx.lines) else ''

                  out.append(Diagnostic(
                     file=str(ctx.file_path),
                     line=c_loc.line,
                     col=c_loc.column,
                     end_line=c_loc.line,
                     end_col=c_loc.column + len(c.spelling),
                     rule=self.id,
                     severity=self.severity,
                     message=('enum value "{}" packed with another value '
                              'on line {} -- each enumerator must be on '
                              'its own line').format(c.spelling, c_loc.line),
                     snippet=line_text.strip(),
                  ))

            last_line = c_loc.line

      return out


RULE = NoPackedEnumValues()
