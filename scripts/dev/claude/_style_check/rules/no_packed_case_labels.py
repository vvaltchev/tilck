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


class NoPackedCaseLabels(Rule):

   id = 'no_packed_case_labels'
   description = (
      'Multiple `case` labels on the same line are forbidden -- each '
      'case label must be on its own line (Q42 hard rule).'
   )
   layers = 'S+T'
   needs_tu = True
   default_score = SCORE_HARD_RULE

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      if ctx.tu is None:
         return []

      out = []
      main_file = str(Path(str(ctx.file_path)).resolve())

      # Map: switch_extent_key -> {line -> first case at that line}.
      # We key by the enclosing switch's location so that cases in
      # different switches don't interfere.
      by_switch = {}

      for cursor in ctx.tu.cursor.walk_preorder():

         if cursor.kind != CursorKind.CASE_STMT:
            continue

         loc = cursor.location

         if loc.file is None:
            continue

         try:

            if str(Path(str(loc.file)).resolve()) != main_file:
               continue

         except Exception:
            continue

         # Find the enclosing switch via lexical extent. We use the
         # case's location plus a heuristic key. For simplicity, key
         # by the line itself globally: two CASE_STMTs on the same
         # line are a packed-labels violation regardless of which
         # switch they belong to (there shouldn't be cross-switch
         # accidental same-line cases in practice).
         line = loc.line

         if line in by_switch:

            other_col = by_switch[line]

            out.append(Diagnostic(
               file=str(ctx.file_path),
               line=line,
               col=loc.column,
               end_line=line,
               end_col=loc.column + 4,
               rule=self.id,
               severity=self.severity,
               message=('multiple case labels packed on line {} (first '
                        'at col {}, this one at col {}) -- each case '
                        'label must be on its own line').format(
                  line, other_col, loc.column
               ),
               snippet=(ctx.lines[line - 1].strip()
                        if line - 1 < len(ctx.lines) else ''),
            ))

         else:
            by_switch[line] = loc.column

      return out


RULE = NoPackedCaseLabels()
