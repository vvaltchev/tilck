# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring

from typing import List

from .base import CheckContext, Diagnostic, LAYER_TOKENS
from .py_common import PyRule


class PyHangingBracket(PyRule):

   id = 'py_hanging_bracket'
   description = (
      'A bracket that ends its line (Style 2) indents its elements one '
      'level past the line it opens on, and closes on a line of its own, '
      'at that line\'s indentation'
   )
   layers = LAYER_TOKENS

   def check_py(self, ctx: CheckContext, py) -> List[Diagnostic]:

      out = []

      for b in py.brackets:

         if not b.hanging:
            continue

         want = b.open_line_indent + ctx.indent

         for ln, col, new_elem in b.line_starts:

            closing = (ln, col) == (b.close_line, b.close_col)

            if closing or not new_elem:
               continue

            if col != want:
               out.append(self.diag(
                  ctx, ln, col + 1,
                  'element at column {}, expected {} (one level past the '
                  'line the `{}` opens on)'.format(col + 1, want + 1, b.char)
               ))

         if not b.close_alone:
            out.append(self.diag(
               ctx, b.close_line, b.close_col + 1,
               'the closing bracket of a hanging `{}` goes on its own line'
               .format(b.char)
            ))
         elif b.close_col != b.open_line_indent:
            out.append(self.diag(
               ctx, b.close_line, b.close_col + 1,
               'closing bracket at column {}, expected {} (the indentation '
               'of the line the `{}` opens on)'.format(
                  b.close_col + 1, b.open_line_indent + 1, b.char
               )
            ))

      return out


class PyAlignedBracket(PyRule):

   id = 'py_aligned_bracket'
   description = (
      'A bracket followed by its first element (Style 1) aligns every '
      'following element under the first, and closes after the last one, '
      'never on a line of its own'
   )
   layers = LAYER_TOKENS

   def check_py(self, ctx: CheckContext, py) -> List[Diagnostic]:

      out = []

      for b in py.brackets:

         if b.hanging:
            continue

         for ln, col, new_elem in b.line_starts:

            closing = (ln, col) == (b.close_line, b.close_col)

            if closing:
               continue

            if new_elem and col != b.first_col:
               out.append(self.diag(
                  ctx, ln, col + 1,
                  'element at column {}, expected {} (under the first '
                  'element after the `{}`)'.format(
                     col + 1, b.first_col + 1, b.char
                  )
               ))

         if b.close_alone:
            out.append(self.diag(
               ctx, b.close_line, b.close_col + 1,
               'the closing bracket of an aligned `{}` goes after its last '
               'element, not on a line of its own'.format(b.char)
            ))

      return out


HANGING = PyHangingBracket()
ALIGNED = PyAlignedBracket()
