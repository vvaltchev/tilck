# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=consider-using-f-string

from typing import List

from .base import (
      Rule,
   Diagnostic,
   Fix,
   CheckContext,
   SEVERITY_WARNING,
   SCORE_STRONG_PREF,
)
from .. import tokens as _tokens_mod


class VoidArglist(Rule):

   id = 'void_arglist'
   description = 'Empty parameter list is (void), never ()'
   layers = 'S+T'
   needs_tu = True
   applies_to = {'.c'}

   severity = SEVERITY_WARNING

   default_score = SCORE_STRONG_PREF


   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      out = []
      seen = set()

      for ext in ctx.extents:

         if ext.kind != 'FUNCTION_DECL':
            continue

         # Find the '(' starting the argument list. cursor.name location
         # points at the function name; the '(' is the next char after
         # the name (possibly with whitespace).
         name_ofs = _tokens_mod.line_col_to_offset(
            ctx.source_text, ext.name_line, ext.name_col
         )

         if name_ofs < 0:
            continue

         # Skip the function name to find the '('
         name_end = name_ofs + len(ext.spelling)
         search_from = name_end

         # Advance over whitespace
         while (search_from < len(ctx.source_text)
                and ctx.source_text[search_from] in ' \t'):
            search_from += 1

         if (search_from >= len(ctx.source_text)
             or ctx.source_text[search_from] != '('):
            continue

         open_ofs = search_from
         close_ofs = _tokens_mod.find_matching_close(
            ctx.source_text, open_ofs
         )

         if close_ofs < 0:
            continue

         between = ctx.source_text[open_ofs + 1:close_ofs].strip()

         if between == '':
            # `()` -- violation. Should be `(void)`.

            key = (ext.name_line, ext.name_col, ext.spelling)

            if key in seen:
               continue

            seen.add(key)

            line_text = ctx.lines[ext.name_line - 1] \
               if ext.name_line - 1 < len(ctx.lines) else ''

            # Build fix: replace () with (void) on the line
            fixes = []

            if line_text:
               # Find the `name()` pattern on the line and replace
               name_pos = line_text.find(ext.spelling + '()')

               if name_pos >= 0:
                  paren_pos = name_pos + len(ext.spelling)
                  fixed = (line_text[:paren_pos]
                           + '(void)'
                           + line_text[paren_pos + 2:])
                  fixes.append(Fix(ext.name_line, ext.name_line,
                                    [fixed],
                                    'replace () with (void)'))

            out.append(Diagnostic(
               file=str(ctx.file_path),
               line=ext.name_line,
               col=ext.name_col,
               end_line=ext.name_line,
               end_col=ext.name_col + len(ext.spelling),
               rule=self.id,
               severity=self.severity,
               message=('function "{}": empty arg list must be (void), '
                        'not ()').format(ext.spelling),
               snippet=line_text.strip(),
               fixes=fixes,
            ))

      return out


RULE = VoidArglist()
