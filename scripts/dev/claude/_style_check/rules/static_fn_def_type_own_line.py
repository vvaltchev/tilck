# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=consider-using-f-string

from typing import List

from .base import (
      Rule,
   Diagnostic,
   CheckContext,
   SEVERITY_WARNING,
   SCORE_STRONG_PREF,
)


class StaticFnDefTypeOwnLine(Rule):

   id = 'static_fn_def_type_own_line'
   description = (
      "Static function definitions: return type/modifiers on their own "
      "line (uniformly, even for short signatures)"
   )
   layers = 'S+T'
   needs_tu = True
   applies_to = {'.c'}

   severity = SEVERITY_WARNING

   default_score = SCORE_STRONG_PREF


   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      out = []
      seen = set()  # (line, name) -- avoid duplicate diags for redeclarations

      for ext in ctx.extents:

         if ext.kind != 'FUNCTION_DECL':
            continue

         if not ext.is_definition:
            continue

         if ext.storage_class != 'static':
            continue

         # The rule only fires when the SIGNATURE WRAPS across multiple
         # lines AND the return type is still on the same line as the
         # name (Style 3, "top-heavy hybrid"). A single-line signature
         # like `static void idle(void *unused)` is fine even though
         # extent.start.line == name.line.

         if ext.start_line != ext.name_line:
            continue  # type already on its own line -- compliant

         if not self._sig_wraps(ctx, ext):
            continue  # single-line signature -- compliant

         key = (ext.start_line, ext.spelling)

         if key in seen:
            continue

         seen.add(key)

         line_text = ctx.lines[ext.start_line - 1] \
            if ext.start_line <= len(ctx.lines) else ''

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=ext.start_line,
            col=1,
            end_line=ext.start_line,
            end_col=len(line_text) + 1,
            rule=self.id,
            severity=self.severity,
            message=('static fn definition "{}": when the signature '
                     'wraps, the return type must be on its own line').
                     format(ext.spelling),
            snippet=line_text.strip(),
         ))

      return out

   @staticmethod
   def _sig_wraps(ctx, ext) -> bool:
      """Return True iff the signature spans more than one line.

      Heuristic: starting from the function name in the start line,
      count parens. If they balance to 0 on the same line, the
      signature is single-line. Otherwise it wraps."""

      if ext.start_line - 1 >= len(ctx.lines):
         return False

      line = ctx.lines[ext.start_line - 1]
      idx = line.find(ext.spelling + '(')

      if idx == -1:
         # Couldn't locate the name(...; assume single-line to avoid
         # noise.
         return False

      depth = 0
      seen_open = False

      for c in line[idx:]:

         if c == '(':
            depth += 1
            seen_open = True
            continue

         if c == ')':
            depth -= 1

            if seen_open and depth == 0:
               return False  # balanced on this line -> single-line sig

      return seen_open  # we opened a paren but didn't close it -> wraps


RULE = StaticFnDefTypeOwnLine()
