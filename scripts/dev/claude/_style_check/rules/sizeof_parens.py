# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring

import re

from typing import List

from .base import (
      Rule,
   Diagnostic,
   Fix,
   CheckContext,
   LAYER_TOKENS,
   SEVERITY_WARNING,
   SCORE_STRONG_PREF,
)
from .. import tokens as _tokens_mod

# Match "sizeof" as a whole word, optional whitespace, then anything
# that is NOT '(' -- that's the violation. The masked source already
# excludes comment / string content so we won't false-positive on
# /* sizeof foo */ or "sizeof foo".
_PAT = re.compile(r'\bsizeof\b([ \t]*)([^( \t\n])')

# Match the argument of a bare `sizeof` -- one or more identifiers
# (covers `sizeof int`, `sizeof unsigned long`, `sizeof foo`).
_ARG = re.compile(r'\bsizeof\s+(\w+(?:\s+\w+)*)')


class SizeofParens(Rule):

   id = 'sizeof_parens'
   description = (
      'sizeof requires parens: write sizeof(X), not sizeof X'
   )
   layers = LAYER_TOKENS

   severity = SEVERITY_WARNING

   default_score = SCORE_STRONG_PREF


   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      masked = _tokens_mod.mask_non_code(ctx.source_text)

      out = []

      for m in _PAT.finditer(masked):

         start = m.start()
         line, col = _tokens_mod.offset_to_line_col(masked, start)

         line_text = ctx.lines[line - 1] if line <= len(ctx.lines) else ''

         # Build a fix: find the argument and wrap it in parens.
         fixes = []
         arg_match = _ARG.match(masked, m.start())

         if arg_match and line_text:
            arg_text = arg_match.group(1)
            old_expr = arg_match.group(0)  # "sizeof  arg"
            new_expr = 'sizeof({})'.format(arg_text)
            fixed_line = line_text.replace(old_expr, new_expr, 1)

            if fixed_line != line_text:
               fixes.append(Fix(line, line,
                                [fixed_line.rstrip()],
                                'add parens: sizeof({})'.format(arg_text)))

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=line,
            col=col,
            end_line=line,
            end_col=col + len('sizeof'),
            rule=self.id,
            severity=self.severity,
            message='sizeof requires parens: write sizeof(X), not sizeof X',
            snippet=line_text.strip(),
            fixes=fixes,
         ))

      return out


RULE = SizeofParens()
