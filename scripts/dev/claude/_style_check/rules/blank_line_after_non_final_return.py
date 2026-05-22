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
   SCORE_HARD_RULE,
)
from .. import tokens as _tokens_mod

# Match a line that ends with a `return ...;` statement. The masked
# source has comments replaced by spaces, so trailing inline comments
# show as trailing whitespace. Allow `return rc;` (with leading
# whitespace) and bare `return;`.
_RETURN_LINE_PAT = re.compile(r'^\s*return\b[^;{]*;\s*$')

# Lines that mark the end of a scope-equivalent region and therefore
# don't need a blank line above. Beyond `}`, the canonical cases are
# `case <expr>:`, `default:`, and goto labels (`<identifier>:`).
_LABEL_LIKE_PAT = re.compile(
   r'^(?:case\b.*:\s*$|default\s*:\s*$|[A-Za-z_]\w*\s*:\s*$)'
)


class BlankLineAfterNonFinalReturn(Rule):

   id = 'blank_line_after_non_final_return'
   description = (
      'Blank line required after every `return` statement except '
      'when followed by `}` (end of enclosing block) or end-of-file '
      '(Q18 hard rule).'
   )
   layers = LAYER_RAW_TEXT
   default_score = SCORE_HARD_RULE

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      masked = _tokens_mod.mask_non_code(ctx.source_text)
      masked_lines = masked.split('\n')
      out = []

      for i, line in enumerate(masked_lines):

         if not _RETURN_LINE_PAT.match(line):
            continue

         if i + 1 >= len(masked_lines):
            continue   # last line of file -- no following line to check

         next_line = masked_lines[i + 1].strip()

         if next_line == '':
            continue   # blank line follows -- compliant

         if next_line.startswith('}'):
            continue   # closing brace -- this return was the last
                       # statement of its enclosing block

         if _LABEL_LIKE_PAT.match(next_line):
            continue   # case/default/goto label -- next scope marker

         line_no = i + 1                # 1-based line number of return
         ret_line_text = ctx.lines[i] if i < len(ctx.lines) else ''

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=line_no,
            col=1,
            end_line=line_no,
            end_col=len(ret_line_text) + 1,
            rule=self.id,
            severity=self.severity,
            message=('return statement on line {} not followed by a '
                     'blank line (next line: `{}`)').format(
               line_no,
               next_line[:40] + ('...' if len(next_line) > 40 else '')
            ),
            snippet=ret_line_text.strip(),
         ))

      return out


RULE = BlankLineAfterNonFinalReturn()
