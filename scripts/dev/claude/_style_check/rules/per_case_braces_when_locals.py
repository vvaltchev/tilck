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

         # Fall-through case: the body is another case/default
         # label. The "real" body belongs to the deepest case in
         # the chain; this label has no decls of its own to scope.
         if body.kind in (CursorKind.CASE_STMT,
                          CursorKind.DEFAULT_STMT):
            continue

         # Walk the body subtree to see whether there's a DECL_STMT
         # at the CASE BODY LEVEL. Stop descending at any nested
         # COMPOUND_STMT -- its decls are inside their own scope
         # (control-flow `{ ... }` bodies, sub-blocks, and GCC
         # statement-expression macros like `MAX(({...}))` or
         # `return_ACPI_STATUS(...)`). The whole point of the rule
         # is the case label's lack of its own scope; decls in a
         # nested compound aren't case-body decls.
         has_decl = _scan_for_decl_outside_compounds(body)

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


def _scan_for_decl_outside_compounds(node) -> bool:
   """Walk `node`'s subtree iteratively, returning True iff a
   DECL_STMT is found WITHOUT crossing into a nested COMPOUND_STMT.

   Rationale: the rule fires on `case X: int y = ...;` because the
   un-braced case body has no scope for the declaration. But if the
   declaration is inside an `if (...) { int y = ...; }` or any other
   construct that opens a `{ ... }` block, that block already
   provides a scope -- the case label's missing-scope issue doesn't
   apply.

   Also catches the GCC statement-expression macro pattern where a
   macro expands to `({ DECL; DECL; result; })`: those decls live
   in the macro's own compound, not in the case body."""

   if node.kind == CursorKind.DECL_STMT:
      return True

   stack = list(node.get_children())

   while stack:

      cur = stack.pop()

      if cur.kind == CursorKind.DECL_STMT:
         return True

      if cur.kind == CursorKind.COMPOUND_STMT:
         continue   # the decls inside are inside a scope -- skip

      for child in cur.get_children():
         stack.append(child)

   return False


RULE = PerCaseBracesWhenLocals()
