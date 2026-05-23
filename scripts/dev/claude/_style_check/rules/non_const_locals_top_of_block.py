# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=broad-except, consider-using-f-string

import re

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

# Statement-shaped macros that the corpus treats as "diagnostic
# preludes" -- they appear at function top alongside decls and do
# NOT count as the "first non-decl statement" that triggers the
# C99 mid-block warning. The user writes ASSERT-style checks
# interleaved with decls intentionally.
_PRELUDE_MACROS = (
   'ASSERT', 'ASSERT_TASK_STATE', 'ASSERT_CURR_TASK_STATE',
   'NO_TEST_ASSERT', 'STATIC_ASSERT', 'STATIC_ASSERT_EXPR',
   'DEBUG_ONLY', 'DEBUG_CHECKED_SUCCESS', 'DEBUG_VALIDATE_STACK',
   'ACPI_FUNCTION_TRACE', 'ACPI_FUNCTION_NAME',
   'BUG_ON', 'BUG',
)

_PRELUDE_PAT = re.compile(
   r'^\s*(?:' + '|'.join(_PRELUDE_MACROS) + r')\s*\('
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
   severity = SEVERITY_WARNING
   default_score = SCORE_STRONG_PREF

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

                  if _is_effectively_const(sub):
                     continue

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

            # Anything else: check whether this is a diagnostic
            # prelude (ASSERT, STATIC_ASSERT, etc.). The corpus
            # places these between the function prologue and the
            # decl block; the user does NOT treat them as the
            # "first non-decl statement" the rule cares about.
            if _is_prelude_child(child, ctx.lines):
               continue

            saw_non_decl = True

      return out


def _is_prelude_child(child, lines) -> bool:
   """Return True iff `child` is a top-of-block diagnostic prelude
   like `ASSERT(...)` or `STATIC_ASSERT(...)`. Detection is source-
   text-based: libclang typically shows the macro's EXPANSION
   (DO_STMT, COMPOUND_STMT, etc.) rather than the call itself, so
   walking the AST kind isn't enough -- we peek at the raw line."""

   try:
      loc = child.location
      line_no = loc.line
   except Exception:
      return False

   if line_no <= 0 or line_no > len(lines):
      return False

   return _PRELUDE_PAT.match(lines[line_no - 1]) is not None


def _is_effectively_const(var_decl) -> bool:
   """True if the variable's declaration carries `const` in any form
   the user considers "const enough" for the mid-block exemption.

   `cursor.type.is_const_qualified()` only catches TOP-LEVEL const
   (e.g. `int *const p`, `const int x`). It misses the common
   `const T *p` form (pointer-to-const), array-of-const, and the
   `static const T[]` table pattern. Those are intent-const --
   the variable is initialised once and used read-only, so the
   user-stated rule treats them as exempt from the C99 mid-block
   ban."""

   try:

      if var_decl.type.is_const_qualified():
         return True

   except Exception:
      pass

   # `static` locals are typically initialised once at first
   # call (or are tables) and don't reassign meaningfully across
   # the function body. Treat as const for this rule.
   try:
      sc = var_decl.storage_class
   except Exception:
      sc = None

   try:

      if sc == clang.cindex.StorageClass.STATIC:
         return True

   except Exception:
      pass

   # Type spelling check: anything containing `const` as a word
   # covers `const T *p`, `const struct T x`, `const T[N]`, etc.
   try:
      ts = var_decl.type.spelling
   except Exception:
      ts = ''

   if ts.startswith('const ') or ' const ' in ts:
      return True

   return False


RULE = NonConstLocalsTopOfBlock()
