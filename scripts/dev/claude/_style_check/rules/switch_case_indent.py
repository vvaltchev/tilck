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


class SwitchCaseIndent(Rule):

   id = 'switch_case_indent'
   description = (
      'Case labels indented +3 from `switch`; Linux-style case-flush-'
      'with-switch is forbidden'
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

         if cursor.kind != CursorKind.SWITCH_STMT:
            continue

         loc = cursor.location

         if loc.file is None:
            continue

         try:

            if str(Path(str(loc.file)).resolve()) != main_file:
               continue

         except Exception:
            continue

         switch_col = loc.column
         expected_case_col = switch_col + 3

         # Walk the switch's body for CASE_STMT and DEFAULT_STMT children
         for child in cursor.walk_preorder():

            if child.kind not in (CursorKind.CASE_STMT,
                                  CursorKind.DEFAULT_STMT):
               continue

            child_loc = child.location

            if child_loc.file is None:
               continue

            try:

               if str(Path(str(child_loc.file)).resolve()) != main_file:
                  continue

            except Exception:
               continue

            actual_col = child_loc.column

            # Allow case labels nested inside an inner switch (we walk
            # preorder; an inner switch's cases shouldn't be measured
            # against the outer switch).
            # Heuristic: skip if the case is not a direct child of the
            # outer switch's body.

            if not _direct_case_of(child, cursor):
               continue

            key = (child_loc.line, child_loc.column)

            if key in seen:
               continue

            seen.add(key)

            if actual_col == expected_case_col:
               continue   # correct indent

            line_text = ctx.lines[child_loc.line - 1] \
               if child_loc.line - 1 < len(ctx.lines) else ''

            out.append(Diagnostic(
               file=str(ctx.file_path),
               line=child_loc.line,
               col=actual_col,
               end_line=child_loc.line,
               end_col=actual_col + 4,    # "case"
               rule=self.id,
               severity=self.severity,
               message=('case label at column {} -- expected column {} '
                        '(switch at column {}, case must be at +3)').format(
                  actual_col, expected_case_col, switch_col
               ),
               snippet=line_text.strip(),
            ))

      return out


def _direct_case_of(case_cursor, switch_cursor) -> bool:
   """Heuristic check: this CASE_STMT belongs to THIS switch (not to
   an inner switch nested inside)."""

   # Walk up the case's lexical parents until we hit a switch.
   # libclang doesn't give us a direct parent pointer cheaply, but
   # we can check whether the case's extent is enclosed within an
   # inner SWITCH_STMT that itself is inside the outer.

   # Simpler approximation: check that no other SWITCH_STMT lies
   # between the case's location and the outer switch's body. We do
   # that by scanning the outer switch's compound-stmt body for
   # nested switches and checking whether the case is inside one.

   outer_body = None

   for child in switch_cursor.get_children():

      if child.kind == CursorKind.COMPOUND_STMT:
         outer_body = child
         break

   if outer_body is None:
      return False

   case_line = case_cursor.location.line
   case_col = case_cursor.location.column

   for grand in outer_body.walk_preorder():

      if grand.kind != CursorKind.SWITCH_STMT:
         continue

      if grand == switch_cursor:
         continue

      # Check if the case is inside this nested switch's extent
      nested_extent = grand.extent

      if (nested_extent.start.line < case_line < nested_extent.end.line):
         return False

      if (nested_extent.start.line == case_line
          and nested_extent.start.column <= case_col):
         return False

   return True


RULE = SwitchCaseIndent()
