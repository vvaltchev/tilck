# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=consider-using-f-string

import re

from typing import List

from .base import (
   Rule,
   Diagnostic,
   CheckContext,
   LAYER_RAW_TEXT,
   SEVERITY_WARNING,
   SCORE_SOFT,
)
from .. import tokens as _tokens_mod

# Q29: TODO / FIXME / NOTE / HACK are the project tag taxonomy.
# `XXX:` is not used in this codebase. Soft nudge: existing XXX
# tags don't get rewritten, but new ones shouldn't be introduced.
_PAT = re.compile(r'\bXXX\s*:')


class CommentTagChoice(Rule):

   id = 'comment_tag_choice'
   description = (
      'Use TODO / FIXME / NOTE / HACK for marker comments; XXX is '
      'not part of this project\'s tag vocabulary (Q29).'
   )
   layers = LAYER_RAW_TEXT
   severity = SEVERITY_WARNING
   default_score = SCORE_SOFT

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      # Only fire inside comments. Use the mask: the mask blanks
      # comments and strings. We want the OPPOSITE -- the comment
      # text -- so we walk the original and skip non-comment regions
      # by referencing the mask's positions. Simpler: find XXX:
      # in the raw text but require the line to also contain `/*`,
      # `*/`, `*` or `//` so we don't match identifiers.
      out = []

      for i, line in enumerate(ctx.lines, start=1):

         m = _PAT.search(line)

         if not m:
            continue

         stripped = line.lstrip()

         is_comment_context = (
            stripped.startswith('/*')
            or stripped.startswith('//')
            or stripped.startswith('*')
            or '/*' in line[:m.start()]
            or '//' in line[:m.start()]
         )

         if not is_comment_context:
            continue

         col = m.start() + 1

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=i,
            col=col,
            end_line=i,
            end_col=col + 3,
            rule=self.id,
            severity=self.severity,
            message=(
               '`XXX:` tag is not used in this project -- prefer '
               'TODO / FIXME / NOTE / HACK'
            ),
            snippet=line.rstrip(),
            suggestion='/* TODO: ... */ or /* FIXME: ... */',
         ))

      return out


RULE = CommentTagChoice()
