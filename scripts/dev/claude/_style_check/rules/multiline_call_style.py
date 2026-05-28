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
   SEVERITY_ERROR,
   SCORE_HARD_RULE,
)
from .. import tokens as _tokens_mod


class MultilineCallStyle(Rule):

   id = 'multiline_call_style'
   description = (
      'Multi-line calls use Style 1 (args aligned to opening paren) '
      'or Style 2 (open paren ends first line, closing ); on its own '
      'line); Style 3 (top-heavy: args on indented lines, ) glued to '
      'last arg) is a HARD violation. Applies to both C and C++.'
   )
   layers = 'S+T'
   needs_tu = True
   applies_to = None   # all C/C++ extensions
   severity = SEVERITY_ERROR
   default_score = SCORE_HARD_RULE

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      if ctx.tu is None:
         return []

      out = []
      seen = set()
      main_file = str(Path(str(ctx.file_path)).resolve())

      for cursor in ctx.tu.cursor.walk_preorder():

         if cursor.kind != CursorKind.CALL_EXPR:
            continue

         loc = cursor.location

         if loc.file is None:
            continue

         try:

            if str(Path(str(loc.file)).resolve()) != main_file:
               continue

         except Exception:
            continue

         start_ofs = _tokens_mod.line_col_to_offset(
            ctx.source_text, loc.line, loc.column
         )

         if start_ofs < 0:
            continue

         open_paren = ctx.source_text.find('(', start_ofs)

         if open_paren < 0:
            continue

         close_paren = _tokens_mod.find_matching_close(
            ctx.source_text, open_paren
         )

         if close_paren < 0:
            continue

         open_line, open_col = _tokens_mod.offset_to_line_col(
            ctx.source_text, open_paren
         )
         close_line, close_col = _tokens_mod.offset_to_line_col(
            ctx.source_text, close_paren
         )

         if open_line == close_line:
            continue  # single-line call

         violation = False
         narrow = (close_line == open_line + 1)

         if narrow:

            # Narrow form: callee( <newline>   arg, arg, arg);
            # Style 3 iff `(` ends the first line (after_open empty)
            # AND `)` is NOT alone on the closing line.
            if open_line - 1 >= len(ctx.lines):
               continue

            open_line_text = ctx.lines[open_line - 1]
            after_open = open_line_text[open_col:]

            if after_open.strip() != '':
               continue  # Style 1: first arg on `(` line

            if close_line - 1 >= len(ctx.lines):
               continue

            close_line_text = ctx.lines[close_line - 1]
            before_close = close_line_text[:close_col - 1]

            if before_close.strip() == '':
               continue  # Style 2 strict

            violation = True

         else:

            # Broad form: close_line > open_line + 1.
            # Style 3 iff each arg occupies its own line AND `)` is
            # glued to the last arg's line.
            children = list(cursor.get_children())

            if len(children) <= 1:
               continue

            args = children[1:]  # children[0] is the callee

            # Skip when any argument spans multiple lines (the
            # legitimate term_write-style shape from screen_tracing.c
            # where sub-expressions are themselves multi-line).
            any_multi_line_arg = False

            for a in args:

               try:

                  if a.extent.start.line != a.extent.end.line:
                     any_multi_line_arg = True
                     break

               except Exception:
                  any_multi_line_arg = True
                  break

            if any_multi_line_arg:
               continue

            arg_lines = [a.extent.start.line for a in args]

            if len(set(arg_lines)) != len(arg_lines):
               continue   # two args share a line

            if close_line != arg_lines[-1]:
               continue   # `)` not glued to last arg

            if not all(open_line < ln <= close_line for ln in arg_lines):
               continue

            violation = True

         if not violation:
            continue

         key = (open_line, open_col)

         if key in seen:
            continue

         seen.add(key)

         if close_line - 1 < len(ctx.lines):
            close_line_text = ctx.lines[close_line - 1]
         else:
            close_line_text = ''

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=close_line,
            col=close_col,
            end_line=close_line,
            end_col=close_col + 1,
            rule=self.id,
            severity=self.severity,
            message=(
               'multi-line call: closing ) must be on its own line '
               '(Style 2), or the first arg must be on the ( line '
               '(Style 1) -- never Style 3 (top-heavy hybrid)'
            ),
            snippet=close_line_text.strip(),
         ))

      return out


RULE = MultilineCallStyle()
