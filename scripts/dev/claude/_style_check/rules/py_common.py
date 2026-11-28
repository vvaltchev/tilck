# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=missing-function-docstring, bad-indentation
"""
The base of the Python rules: they read the PyContext (pyctx.py), and a
file Python itself cannot parse gets a `parse` diagnostic instead.

The style they check is the one inferred from the author's own Python, in
docs/plans/python-style.md.
"""

from typing import List, Optional

from .base import Rule, Diagnostic, CheckContext
from ..languages import PYTHON


class PyRule(Rule):

   languages = frozenset({PYTHON})

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      if ctx.py is None or ctx.py.error:
         return []

      return self.check_py(ctx, ctx.py)

   def check_py(self, ctx: CheckContext, py) -> List[Diagnostic]:
      raise NotImplementedError(self.id)

   def diag(self,
            ctx: CheckContext,
            line: int,
            col: int,
            message: str,
            suggestion: Optional[str] = None,
            end_line: Optional[int] = None) -> Diagnostic:

      text = ctx.lines[line - 1] if 0 < line <= len(ctx.lines) else ''

      return Diagnostic(
         file=str(ctx.file_path),
         line=line,
         col=col,
         end_line=end_line or line,
         end_col=len(text) + 1,
         rule=self.id,
         severity=self.severity,
         message=message,
         snippet=text.rstrip(),
         suggestion=suggestion,
      )
