# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring

from typing import List

from .base import CheckContext, Diagnostic, LAYER_RAW_TEXT
from .py_common import PyRule

SPDX = '# SPDX-License-Identifier: BSD-2-Clause'


class PySpdxHeader(PyRule):

   id = 'py_spdx_header'
   description = (
      'Python files start with "{}" (after the shebang, if any)'.format(SPDX)
   )
   layers = LAYER_RAW_TEXT

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      # Works on the text alone, so it holds even for a file Python
      # cannot parse. An empty file (a bare package __init__.py) has
      # nothing to license.
      if not ctx.source_text.strip():
         return []

      n = 2 if ctx.lines[0].startswith('#!') else 1

      if len(ctx.lines) >= n and ctx.lines[n - 1].rstrip() == SPDX:
         return []

      return [self.diag(
         ctx, n, 1, 'line {} must be "{}"'.format(n, SPDX)
      )]


RULE = PySpdxHeader()
