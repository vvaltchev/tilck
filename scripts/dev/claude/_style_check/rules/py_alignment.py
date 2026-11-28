# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring

"""
Alignment and spacing in Python, as in the author's C: one form per
construct, columns aligned where lines are related. See
docs/plans/python-style.md, 3.3-3.4.
"""

import ast
import tokenize

from typing import List

from .base import (
   CheckContext,
   Diagnostic,
   LAYER_TOKENS,
   LAYER_STRUCTURAL,
   SEVERITY_WARNING,
   SCORE_MEDIUM_PREF,
   SCORE_SOFT,
)
from .py_common import PyRule

# A line break after one of these tokens is not a backslash continuation:
# the line ended, or held no code. Nor is one before a token carrying no
# code.
_LINE_ENDS = (tokenize.NL, tokenize.NEWLINE, tokenize.COMMENT,
              tokenize.INDENT, tokenize.DEDENT, tokenize.ENCODING)
_NO_CODE = (tokenize.INDENT, tokenize.DEDENT, tokenize.ENDMARKER)


class PyDictColonUniform(PyRule):

   id = 'py_dict_colon_uniform'
   description = (
      'One dict literal uses one colon form: `k: v`, `k : v`, or its '
      'values aligned in a column'
   )
   layers = LAYER_STRUCTURAL
   severity = SEVERITY_WARNING
   default_score = SCORE_SOFT

   def check_py(self, ctx: CheckContext, py) -> List[Diagnostic]:

      out = []

      for node in ast.walk(py.tree):

         if not isinstance(node, ast.Dict) or len(node.keys) < 2:
            continue

         forms = set()
         value_cols = set()
         multi_line = node.end_lineno > node.lineno

         for k, v in zip(node.keys, node.values):

            if k is None or k.end_lineno != v.lineno:
               forms.add(None)
               continue

            seg = ctx.lines[k.end_lineno - 1][k.end_col_offset:v.col_offset]
            before = len(seg) - len(seg.lstrip())
            after = len(seg.rstrip()) and len(seg) - len(seg.rstrip())
            forms.add((min(before, 1), after))
            value_cols.add(v.col_offset)

         if None in forms or len(forms) <= 1:
            continue

         if multi_line and len(value_cols) == 1:
            continue  # values aligned in one column

         out.append(self.diag(
            ctx, node.lineno, node.col_offset + 1,
            'the dict mixes `k: v` and `k : v`: use one form, or align the '
            'values in a column'
         ))

      return out


class PyBackslashAlign(PyRule):

   id = 'py_backslash_align'
   description = (
      'The backslashes of consecutive continued lines are aligned in one '
      'column, as in a multi-line C macro'
   )
   layers = LAYER_TOKENS
   severity = SEVERITY_WARNING
   default_score = SCORE_MEDIUM_PREF

   def check_py(self, ctx: CheckContext, py) -> List[Diagnostic]:

      # Physical lines ending in a continuation backslash: the line
      # changes between two tokens with no NL/NEWLINE between them.
      cont = []
      prev = None

      for t in py.tokens:

         if (prev is not None and t.start[0] > prev.end[0] and
             prev.type not in _LINE_ENDS and t.type not in _NO_CODE):

            cont.append(prev.end[0])

         prev = t

      out = []
      run = []

      for ln in sorted(set(cont)) + [None]:

         if run and (ln is None or ln != run[-1] + 1):
            cols = {len(ctx.lines[n - 1].rstrip()) - 1 for n in run}
            if len(run) > 1 and len(cols) > 1:
               out.append(self.diag(
                  ctx, run[0], 1,
                  'the continuation backslashes of lines {}-{} are not '
                  'aligned in one column'.format(run[0], run[-1])
               ))
            run = []

         if ln is not None:
            run.append(ln)

      return out


class PyAssertParens(PyRule):

   id = 'py_assert_parens'
   description = (
      'An assert without a message is written like a call: `assert(x)`'
   )
   layers = LAYER_STRUCTURAL
   severity = SEVERITY_WARNING
   default_score = SCORE_SOFT

   def check_py(self, ctx: CheckContext, py) -> List[Diagnostic]:

      out = []

      for node in ast.walk(py.tree):

         if not isinstance(node, ast.Assert) or node.msg is not None:
            continue

         text = ctx.lines[node.lineno - 1][node.col_offset:]

         if not text.startswith('assert('):
            out.append(self.diag(
               ctx, node.lineno, node.col_offset + 1,
               'write `assert(...)`, with the condition in parentheses'
            ))

      return out


DICT_COLON = PyDictColonUniform()
BACKSLASH = PyBackslashAlign()
ASSERT = PyAssertParens()
