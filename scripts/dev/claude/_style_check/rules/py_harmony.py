# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring

"""
Harmony for Python: the model of harmony.py over Python's functions.

A function's visible lines are its body's non-blank lines, comments and
docstrings included, but not the lines of a def or class nested in it
(those are judged on their own) nor lines made only of closing brackets.
Its normal lines are the first lines of its statements, minus comments
and docstrings: a continuation line is shaped by the statement it
continues, not chosen on its own.

Python excuses what C excuses: message calls (print and its family,
raise, writes), string-heavy lines, and lines ending in a comma or
carrying many arguments.
"""

import ast
import re
import tokenize

from typing import Dict, List, Set

from .base import (
   CheckContext,
   Diagnostic,
   SEVERITY_WARNING,
   SCORE_SOFT,
)
from .py_common import PyRule
from .. import harmony

MAX_COMMAS = 4
STRING_HEAVY = 0.40

_MESSAGE_PAT = re.compile(
   r'^\s*(?:raise\b|[\w.]*print\w*\s*\(|[\w.]*write\s*\(|'
   r'[\w.]*fail\w*\s*\()'
)

_CLOSING_ONLY = re.compile(r'^\s*[)\]}]+[,:]?\s*$')

_STRING_TYPES = {tokenize.STRING}

for _name in ('FSTRING_START', 'FSTRING_MIDDLE', 'FSTRING_END'):
   if hasattr(tokenize, _name):
      _STRING_TYPES.add(getattr(tokenize, _name))

_DEFS = (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)


class PyHarmony(PyRule):

   id = 'py_harmony'
   description = (
      'An under-80-col line that is a statistical outlier (IQR fence) '
      'within its function, not part of a recognized pattern (message '
      'calls, string-heavy lines, alternating pairs), and has no length '
      'partner may indicate visual disharmony'
   )
   layers = 'S+R'
   severity = SEVERITY_WARNING
   default_score = SCORE_SOFT

   def check_py(self, ctx: CheckContext, py) -> List[Diagnostic]:

      lines = ctx.lines
      strings = _string_chars(py)
      commas = _commas(py)
      docs = _docstring_lines(py)

      def exempt(n):

         text = lines[n - 1]
         stripped = text.strip()

         return (_MESSAGE_PAT.match(text) is not None or
                 strings.get(n, 0) > len(stripped) * STRING_HEAVY or
                 stripped.endswith(',') or
                 commas.get(n, 0) > MAX_COMMAS)

      out = []

      for fn in ast.walk(py.tree):

         if not isinstance(fn, (ast.FunctionDef, ast.AsyncFunctionDef)):
            continue

         nested = _nested_def_lines(fn)
         visual_lens = []
         normal = []

         for n in range(fn.body[0].lineno, fn.end_lineno + 1):

            text = lines[n - 1]
            stripped = text.strip()

            if not stripped or n in nested or _CLOSING_ONLY.match(text):
               continue

            visual_lens.append(len(text.rstrip()))

            if stripped.startswith('#') or n in docs:
               continue

            if py.logical_start.get(n) == n:
               normal.append((n, len(text.rstrip())))

         for o in harmony.outliers(visual_lens, normal, exempt):
            out.append(Diagnostic(
               file=str(ctx.file_path),
               line=o.line,
               col=1,
               end_line=o.line,
               end_col=o.length + 1,
               rule=self.id,
               severity=self.severity,
               message=o.message(),
               snippet=lines[o.line - 1].rstrip(),
               is_gradient=True,
               prettiness_cost=o.cost,
            ))

      return out


def _nested_def_lines(fn) -> Set[int]:
   """The lines of the defs and classes nested in `fn`'s body, decorators
   included."""

   out = set()

   for node in ast.walk(fn):

      if node is fn or not isinstance(node, _DEFS):
         continue

      first = min([node.lineno] + [d.lineno for d in node.decorator_list])
      out.update(range(first, node.end_lineno + 1))

   return out


def _docstring_lines(py) -> Set[int]:

   out = set()

   for node in ast.walk(py.tree):

      if not isinstance(node, _DEFS + (ast.Module,)) or not node.body:
         continue

      first = node.body[0]

      if (isinstance(first, ast.Expr) and
          isinstance(first.value, ast.Constant) and
          isinstance(first.value.value, str)):

         out.update(range(first.lineno, first.end_lineno + 1))

   return out


def _string_chars(py) -> Dict[int, int]:
   """Line -> how many of its characters are string literals. A string
   spanning lines makes every line it covers all string."""

   out = {}

   for t in py.tokens:

      if t.type not in _STRING_TYPES:
         continue

      if t.start[0] == t.end[0]:
         n = t.start[0]
         out[n] = out.get(n, 0) + t.end[1] - t.start[1]
         continue

      for n in range(t.start[0], t.end[0] + 1):
         out[n] = len(py.lines[n - 1])

   return out


def _commas(py) -> Dict[int, int]:

   out = {}

   for t in py.tokens:
      if t.type == tokenize.OP and t.string == ',':
         out[t.start[0]] = out.get(t.start[0], 0) + 1

   return out


RULE = PyHarmony()
