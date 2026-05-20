# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring

from typing import List

from .base import (
   Rule,
   Diagnostic,
   CheckContext,
   LAYER_COMMENTS,
)


class CommentBlockMultilineFormat(Rule):

   id = 'comment_block_multiline_format'
   description = (
      'Multi-line block comments: interior lines must start with " * "'
   )
   layers = LAYER_COMMENTS
   needs_comments = True

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      out = []

      for cr in ctx.comments:

         if cr.kind != 'block':
            continue

         if cr.start_line == cr.end_line:
            continue  # single-line /* ... */, not subject to this rule

         # Interior lines: cr.start_line + 1 ... cr.end_line
         # Each must begin (after optional whitespace) with '*' followed
         # by either a space, EOL, or '/' (closing line).

         for ln in range(cr.start_line + 1, cr.end_line + 1):

            line_text = ctx.lines[ln - 1] \
               if ln - 1 < len(ctx.lines) else ''

            stripped = line_text.lstrip(' \t')

            # Closing line "<ws>*/<...>" is fine.
            if stripped.startswith('*/'):
               continue

            # Body line: must start with '*'.
            if not stripped.startswith('*'):
               out.append(Diagnostic(
                  file=str(ctx.file_path),
                  line=ln,
                  col=1,
                  end_line=ln,
                  end_col=len(line_text) + 1,
                  rule=self.id,
                  severity=self.severity,
                  message=('interior line of multi-line block comment '
                           'must start with " * "'),
                  snippet=line_text,
               ))
               continue

            # After the leading '*', the next char must be space, EOL,
            # or '/' (for the closer, already handled above).
            after = stripped[1:]

            if after == '' or after[0] in (' ', '\t'):
               continue

            # '*X' where X is not whitespace -- malformed.
            out.append(Diagnostic(
               file=str(ctx.file_path),
               line=ln,
               col=1,
               end_line=ln,
               end_col=len(line_text) + 1,
               rule=self.id,
               severity=self.severity,
               message=('multi-line comment line should be " * " or '
                        '" *" (got no space after star)'),
               snippet=line_text,
            ))

      return out


RULE = CommentBlockMultilineFormat()
