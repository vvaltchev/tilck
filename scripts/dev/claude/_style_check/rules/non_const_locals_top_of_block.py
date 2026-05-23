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
   SEVERITY_ERROR,
   SEVERITY_WARNING,
   SCORE_HARD_RULE,
   SCORE_STRONG_PREF,
   SCORE_SOFT,
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

# Decl-introducing prelude pattern: an UPPERCASE_MACRO_NAME(...)
# at the start of a line. libclang represents the macro's
# expansion as DECL_STMTs at the call-site line, so we recognise
# these by source text and skip the decls they introduce.
# Examples in Tilck: DEBUG_SAVE_ESP(), CREATE_SHADOW_STACK(...),
# DECLARE_SHADOW_STACK(...), and similar macros that expand to
# declarations at function top.
_MACRO_DECL_PRELUDE_PAT = re.compile(
   r'^\s*[A-Z_][A-Z0-9_]*\s*\('
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

         # Two-flag classifier walking down the compound statement:
         #
         #   saw_real_non_decl  -- a non-prelude statement was seen
         #                         (function call, control flow, ...)
         #   saw_prelude        -- an ASSERT-style prelude was seen
         #                         but no other non-decl statement
         #
         # Decision matrix when a DECL_STMT is encountered:
         #
         #   neither seen          -> still in the leading decl block;
         #                            this decl is fine.
         #   saw_prelude only      -> init'd decl gets a small SOFT
         #                            penalty (-0.5): never fully
         #                            tolerated, but the line-savings
         #                            argument applies so the cost is
         #                            modest. The caller's judgment
         #                            decides whether to move the
         #                            decl up (with init) or split
         #                            (bare up + assign below).
         #                            BARE decl is HARD: no line-
         #                            savings argument when there's
         #                            no initializer.
         #   saw_real_non_decl     -> any non-const decl is SOFT-
         #                            STRONG drift (Q15 standard
         #                            mid-block ban).
         saw_real_non_decl = False
         saw_prelude = False

         for child in cursor.get_children():

            if child.kind == CursorKind.DECL_STMT:

               if not (saw_real_non_decl or saw_prelude):
                  continue   # still in the leading decl block

               # Decl-introducing macro prelude: when the source
               # line at the DECL_STMT's location is an uppercase
               # macro call (DEBUG_SAVE_ESP(), CREATE_SHADOW_STACK,
               # etc.), the decls came from the macro's expansion.
               # Conceptually the whole macro is a prelude --
               # skip the decls it introduces.
               if _is_macro_decl_prelude(child, ctx.lines):
                  continue

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

                  has_init = _decl_has_init(child)

                  # Classify the violation. Three flavors with
                  # different severities / scores:
                  #
                  #   hard      = severity error,   score -10  -- bare decl after prelude only
                  #   strong    = severity warning, score -3   -- any decl after real non-decl
                  #   small     = severity warning, score -0.5 -- init'd decl after prelude only
                  if saw_real_non_decl:

                     severity = self.severity
                     score = SCORE_STRONG_PREF
                     msg = (
                        'non-const local "{}" declared after non-'
                        'declaration statements; move to top of '
                        'enclosing scope (function body, control '
                        'body, or a {{ }} sub-block)'
                     ).format(sub.spelling)

                  elif has_init:

                     # init'd decl after prelude: small soft penalty.
                     # Whether to actually move it is a judgment call
                     # (dependency on the ASSERT'd state vs harmony
                     # with the leading decl block).
                     severity = self.severity
                     score = SCORE_SOFT
                     msg = (
                        'init\'d local "{}" declared after a '
                        'diagnostic-prelude (ASSERT/STATIC_ASSERT/...) '
                        '-- consider moving this decl above the '
                        'prelude (whole decl if the initializer does '
                        'not depend on the asserted state; bare decl + '
                        'assignment-after otherwise)'
                     ).format(sub.spelling)

                  else:

                     # bare decl after prelude only -- the hard case.
                     # No line-savings argument applies.
                     severity = SEVERITY_ERROR
                     score = SCORE_HARD_RULE
                     msg = (
                        'bare local "{}" declared after a diagnostic-'
                        'prelude (ASSERT/STATIC_ASSERT/...) without an '
                        'initializer -- move to top of the enclosing '
                        'scope. The line-savings argument that allows '
                        'init\'d decls after preludes does NOT apply '
                        'when the decl has no initializer.'
                     ).format(sub.spelling)

                  line_text = ctx.lines[sub_loc.line - 1] \
                     if sub_loc.line - 1 < len(ctx.lines) else ''

                  out.append(Diagnostic(
                     file=str(ctx.file_path),
                     line=sub_loc.line,
                     col=sub_loc.column,
                     end_line=sub_loc.line,
                     end_col=sub_loc.column + len(sub.spelling),
                     rule=self.id,
                     severity=severity,
                     score=score,
                     message=msg,
                     snippet=line_text.strip(),
                  ))

               continue

            # Non-decl child. Classify: prelude or real statement.
            if _is_prelude_child(child, ctx.lines):
               saw_prelude = True
            else:
               saw_real_non_decl = True

      return out


def _decl_has_init(decl_stmt) -> bool:
   """True iff the DECL_STMT contains an initializer.

   libclang's VAR_DECL extent only covers the declarator (`int x`)
   -- the `= ...` initializer can be missing from the cursor's
   tokens AND its children, especially for struct-init syntax
   (`= { .field = ... }`). So we look at the PARENT DECL_STMT,
   whose token stream does include the full declaration including
   the initializer.

   Limitation: multi-variable decls like `int a, b = 5;` are
   treated as fully-initialized (the `=` is detected even though
   only `b` has an init). This is rare in the Tilck corpus and
   the user can split such decls if they want strict checking."""

   depth_paren = 0
   depth_brace = 0

   try:
      toks = list(decl_stmt.get_tokens())
   except Exception:
      return False

   for t in toks:

      sp = t.spelling

      if sp == '(':
         depth_paren += 1
      elif sp == ')':
         depth_paren -= 1
      elif sp == '{':
         depth_brace += 1
      elif sp == '}':
         depth_brace -= 1
      elif sp == '=' and depth_paren == 0 and depth_brace == 0:
         return True
      elif sp == ';' and depth_paren == 0 and depth_brace == 0:
         return False

   return False


def _is_macro_decl_prelude(decl_stmt, lines) -> bool:
   """True iff the DECL_STMT's source line is an UPPERCASE_NAME(...)
   macro call. The decls came from the macro's expansion and the
   whole call is a prelude-style construct."""

   try:
      line_no = decl_stmt.location.line
   except Exception:
      return False

   if line_no <= 0 or line_no > len(lines):
      return False

   return _MACRO_DECL_PRELUDE_PAT.match(lines[line_no - 1]) is not None


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
