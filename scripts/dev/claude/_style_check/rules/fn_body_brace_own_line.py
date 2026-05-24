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
from .. import tokens as _tokens_mod


class FnBodyBraceOwnLine(Rule):

   id = 'fn_body_brace_own_line'
   description = 'Function body opening brace { goes on its own line'
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

         if not ext.is_definition:
            continue

         # Walk from the function name's `(` to its matching `)`, then
         # forward to find the opening `{`. The text between `)` and
         # `{` must contain at least one newline.
         name_ofs = _tokens_mod.line_col_to_offset(
            ctx.source_text, ext.name_line, ext.name_col
         )

         if name_ofs < 0:
            continue

         open_paren = ctx.source_text.find('(', name_ofs)

         if open_paren < 0:
            continue

         close_paren = _tokens_mod.find_matching_close(
            ctx.source_text, open_paren
         )

         if close_paren < 0:
            continue

         # Find the opening '{' of the body
         brace_ofs = ctx.source_text.find('{', close_paren)

         if brace_ofs < 0:
            continue

         # Anything between `)` and `{` other than whitespace + newline
         # is unusual (e.g. K&R style with arg decls -- not used in this
         # project). Detect by checking for at least one '\n' in that
         # range.
         between = ctx.source_text[close_paren + 1:brace_ofs]

         if '\n' in between:
            continue  # `{` on its own line -- compliant

         # One-liner function defs (e.g. `void foo() { }` or
         # `int bar() { return 0; }`) are an accepted pattern for
         # trivial stubs. Skip when the matching `}` is on the same
         # line as the opening `{`.
         close_brace = _tokens_mod.find_matching_close(
            ctx.source_text, brace_ofs, '{', '}'
         )

         if close_brace >= 0:
            body_span = ctx.source_text[brace_ofs:close_brace + 1]

            if '\n' not in body_span:
               continue

         # No newline between `)` and `{`: brace is on the same line
         # as the signature.
         line, col = _tokens_mod.offset_to_line_col(
            ctx.source_text, brace_ofs
         )

         key = (line, ext.spelling)

         if key in seen:
            continue

         seen.add(key)

         line_text = ctx.lines[line - 1] \
            if line - 1 < len(ctx.lines) else ''

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=line,
            col=col,
            end_line=line,
            end_col=col + 1,
            rule=self.id,
            severity=self.severity,
            message=('function "{}": body brace {{ must be on its own line').
                     format(ext.spelling),
            snippet=line_text.strip(),
         ))

      return out


RULE = FnBodyBraceOwnLine()
