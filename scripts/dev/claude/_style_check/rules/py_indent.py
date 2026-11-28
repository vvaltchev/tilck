# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring

import tokenize

from typing import List

from .base import CheckContext, Diagnostic, LAYER_TOKENS
from .py_common import PyRule


class PyIndent(PyRule):

   id = 'py_indent'
   description = (
      'Each block is indented by exactly one level (--indent width), with '
      'spaces only'
   )
   layers = LAYER_TOKENS

   def check_py(self, ctx: CheckContext, py) -> List[Diagnostic]:

      out = []
      levels = [0]

      for t in py.tokens:

         if t.type == tokenize.INDENT:

            if '\t' in t.string:
               out.append(self.diag(ctx, t.start[0], 1,
                                    'tab in indentation'))

            width = len(t.string.expandtabs(ctx.indent)) - levels[-1]
            levels.append(levels[-1] + width)

            if width != ctx.indent:
               out.append(self.diag(
                  ctx, t.start[0], 1,
                  'block indented by {} spaces, expected {}'.format(
                     width, ctx.indent
                  )
               ))

         elif t.type == tokenize.DEDENT:
            levels.pop()

      return out


RULE = PyIndent()
