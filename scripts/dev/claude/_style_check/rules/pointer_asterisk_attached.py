# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=broad-except, consider-using-f-string

import re

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
from .. import tokens as _tokens_mod

# Text-based pattern for C++ reference `&` attached to type instead
# of variable name.  Matches `Type& var` but not `&var` (address-of)
# or `&&` (rvalue ref / logical AND).
_REF_ATTACHED_TO_TYPE = re.compile(r'\b(\w+)&\s+(\w+)')


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

      # In C++ files, also check for reference `&` attached to the
      # type rather than the variable name (`Type& var` -> `Type &var`).
      # Uses a text-based scan on masked source to avoid false positives
      # inside comments and strings.
      if ctx.is_cpp:
         self._check_cpp_ref_attached_to_type(ctx, out, seen)

      return out

   def _check_cpp_ref_attached_to_type(self, ctx, out, seen):

      masked = _tokens_mod.mask_non_code(ctx.source_text)

      for i, (masked_line, raw_line) in enumerate(
         zip(masked.split('\n'), ctx.lines), start=1
      ):

         for m in _REF_ATTACHED_TO_TYPE.finditer(masked_line):

            type_name = m.group(1)
            var_name = m.group(2)

            # Skip keywords that are not type names
            if type_name in ('return', 'goto', 'case', 'sizeof',
                             'if', 'while', 'for', 'switch',
                             'delete', 'throw', 'new'):
               continue

            amp_pos = m.start() + len(type_name)  # 0-based position of &
            key = (i, amp_pos + 1, var_name)

            if key in seen:
               continue

            seen.add(key)

            # Build fix: move `&` from type to variable name
            # `Type& var` -> `Type &var`
            fixed_line = (raw_line[:amp_pos] +
                          ' &' +
                          raw_line[amp_pos + 2:])

            out.append(Diagnostic(
               file=str(ctx.file_path),
               line=i,
               col=amp_pos + 1,
               end_line=i,
               end_col=amp_pos + 2,
               rule=self.id,
               severity=self.severity,
               message=(
                  'reference `&` attached to type instead of '
                  'variable name "{}" -- use `Type &{}`'
               ).format(var_name, var_name),
               snippet=raw_line.strip(),
               fixes=[Fix(i, i, [fixed_line])],
            ))


RULE = PointerAsteriskAttached()
