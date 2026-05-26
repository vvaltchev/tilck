# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=broad-except, consider-using-f-string

from pathlib import Path
from typing import List

import clang.cindex
from clang.cindex import CursorKind, TypeKind

from .base import (
   Rule,
   Diagnostic,
   Fix,
   CheckContext,
   SEVERITY_WARNING,
   SCORE_STRONG_PREF,
)


def _is_pointer_typed(cursor) -> bool:
   """Return True if the cursor's type is a pointer (including pointer-
   to-const, pointer-to-pointer, etc.). Function pointers are skipped
   for now (their syntax is `(*name)(args)` which the rule handles
   differently)."""

   try:
      t = cursor.type
   except clang.cindex.LibclangError:
      return False

   if t.kind != TypeKind.POINTER:
      return False

   # Skip function pointers
   try:
      pointee = t.get_pointee()
   except clang.cindex.LibclangError:
      return False

   if pointee.kind in (TypeKind.FUNCTIONPROTO, TypeKind.FUNCTIONNOPROTO):
      return False

   return True


class PointerAsteriskAttached(Rule):

   id = 'pointer_asterisk_attached'
   description = (
      'Pointer `*` must be attached to the variable name (`Type *var`, '
      'not `Type* var` or `Type * var`)'
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

         if cursor.kind not in (
            CursorKind.VAR_DECL,
            CursorKind.PARM_DECL,
            CursorKind.FIELD_DECL,
         ):
            continue

         loc = cursor.location

         if loc.file is None:
            continue

         try:

            if str(Path(str(loc.file)).resolve()) != main_file:
               continue

         except Exception:
            continue

         if not _is_pointer_typed(cursor):
            continue

         # cursor.location points at the variable name. The char
         # immediately preceding the name must be `*` -- otherwise
         # the name is separated from the `*` by whitespace.
         line_idx = loc.line - 1

         if line_idx < 0 or line_idx >= len(ctx.lines):
            continue

         line = ctx.lines[line_idx]
         col_zero = loc.column - 1

         if col_zero <= 0 or col_zero > len(line):
            continue

         prev_char = line[col_zero - 1]

         if prev_char == '*':
            continue  # attached -- compliant

         # The name is not directly after `*`. Look back for a `*`
         # in the leading content; if found, the gap between `*`
         # and the name is the violation.
         before = line[:col_zero]
         star_pos = before.rfind('*')

         if star_pos < 0:
            continue  # no `*` found -- shouldn't happen for pointer decl

         between = before[star_pos + 1:]

         if between.strip() != '':
            continue  # something non-whitespace between -- not the simple case

         # Confirmed: `*` is separated from name by whitespace.
         key = (loc.line, loc.column, cursor.spelling)

         if key in seen:
            continue

         seen.add(key)

         fixed_line = line[:star_pos + 1] + line[col_zero:]

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=loc.line,
            col=star_pos + 1,
            end_line=loc.line,
            end_col=col_zero + 1,
            rule=self.id,
            severity=self.severity,
            message=('pointer `*` separated from variable name "{}" by '
                     'whitespace -- use `Type *{}`').format(
               cursor.spelling, cursor.spelling
            ),
            snippet=line.strip(),
            fixes=[Fix(loc.line, loc.line, [fixed_line])],
         ))

      return out


RULE = PointerAsteriskAttached()
