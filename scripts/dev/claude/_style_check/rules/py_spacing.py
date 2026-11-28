# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring

"""
Spaces inside a statement, where Python follows PEP 8 (the author's
decision, docs/plans/python-style.md 3.4): `key=value` in calls,
`param=default` in definitions but `param: T = default` once annotated,
and `name: T` for every annotation.

Each rule works on the tokens around the `=` or `:`, so a value in
brackets (`help=('...')`) or an annotation in several parts
(`Dict[str, int]`) is measured from the token that really ends it. A
side broken across lines is not a spacing matter and is left alone.
Every finding carries its fix, so `check --diff` rewrites the line.
"""

import ast

from typing import Dict, List, Optional, Tuple

from .base import CheckContext, Diagnostic, Fix, LAYER_TOKENS
from .py_common import PyRule, bodies

Edit = Tuple[int, int, str]      # (start col, end col, text) on one line

_FUNCTIONS = (ast.FunctionDef, ast.AsyncFunctionDef, ast.Lambda)


class _SpacingRule(PyRule):
   """Collects the edits of each line, so that one fix rewrites every
   site of the rule on that line."""

   layers = LAYER_TOKENS

   def check_py(self, ctx: CheckContext, py) -> List[Diagnostic]:

      sites = []
      edits: Dict[int, List[Edit]] = {}

      for line, col, message, line_edits in self.sites(ctx, py):

         sites.append((line, col, message))

         for e in line_edits:
            edits.setdefault(e[0], []).append(e[1:])

      out = []

      for line, col, message in sorted(sites):
         d = self.diag(ctx, line, col, message)
         d.fixes = [_fix(ctx, line, edits[line], self.id)]
         out.append(d)

      return out

   def sites(self, ctx: CheckContext, py):
      raise NotImplementedError(self.id)


def _fix(ctx: CheckContext, line: int, edits: List[Edit], why: str) -> Fix:

   text = ctx.lines[line - 1]

   for start, end, new in sorted(edits, reverse=True):
      text = text[:start] + new + text[end:]

   return Fix(line, line, [text], why)


def _respace(py, left: int, mid: int, before: int, after: int):
   """The edit giving tokens[mid] (an operator) `before` spaces after
   tokens[left] and `after` spaces before the next code token, as
   (line, start col, end col, text); None when the spacing is already
   that, or when a side is broken across lines."""

   right = py.next_code(mid)

   if left < 0 or right < 0:
      return None

   lt, mt, rt = py.tokens[left], py.tokens[mid], py.tokens[right]

   if not lt.end[0] == mt.start[0] == mt.end[0] == rt.start[0]:
      return None

   want = ' ' * before + mt.string + ' ' * after

   if py.lines[mt.start[0] - 1][lt.end[1]:rt.start[1]] == want:
      return None

   return (mt.start[0], lt.end[1], rt.start[1], want)


def _operator_after(py, i: int, op: str) -> int:

   j = py.next_code(i) if i >= 0 else -1

   if j < 0 or py.tokens[j].string != op:
      return -1

   return j


class PyCallKwSpacing(_SpacingRule):

   id = 'py_call_kw_spacing'
   description = (
      'A keyword argument is written `key=value`, with no space around '
      'the `=` (PEP 8)'
   )

   def sites(self, ctx: CheckContext, py):

      for node in ast.walk(py.tree):

         if not isinstance(node, (ast.Call, ast.ClassDef)):
            continue

         for k in node.keywords:

            if k.arg is None:
               continue        # **kwargs

            name = py.token_starts.get((k.lineno, k.col_offset), -1)
            eq = _operator_after(py, name, '=')
            edit = _respace(py, name, eq, 0, 0) if eq >= 0 else None

            if edit:
               yield (k.lineno, k.col_offset + 1,
                      'write `{}=...`, with no space around `=`'
                      .format(k.arg), [edit])


class PyDefDefaultSpacing(_SpacingRule):

   id = 'py_def_default_spacing'
   description = (
      'A parameter default is written `param=default`, and `param: T = '
      'default` when the parameter is annotated (PEP 8)'
   )

   def sites(self, ctx: CheckContext, py):

      for node in ast.walk(py.tree):

         if not isinstance(node, _FUNCTIONS):
            continue

         args = node.args
         pos = args.posonlyargs + args.args
         defaults = list(zip(pos[len(pos) - len(args.defaults):],
                             args.defaults))
         defaults += [(a, d) for a, d in zip(args.kwonlyargs,
                                             args.kw_defaults) if d]

         for a, _ in defaults:

            if a.annotation is not None:
               end = (a.annotation.end_lineno, a.annotation.end_col_offset)
               left = py.token_ends.get(end, -1)
               want, form = 1, '`{}: T = ...`'
            else:
               left = py.token_starts.get((a.lineno, a.col_offset), -1)
               want, form = 0, '`{}=...`'

            eq = _operator_after(py, left, '=')
            edit = _respace(py, left, eq, want, want) if eq >= 0 else None

            if edit:
               yield (a.lineno, a.col_offset + 1,
                      'write the default as ' + form.format(a.arg), [edit])


class PyAnnotationSpacing(_SpacingRule):

   id = 'py_annotation_spacing'
   description = (
      'An annotation is written `name: T`: the colon right after the '
      'name, one space after it. A column of annotated assignments may '
      'pad after the colon to keep their types aligned'
   )

   def sites(self, ctx: CheckContext, py):

      for node in ast.walk(py.tree):

         if not isinstance(node, ast.arg) or node.annotation is None:
            continue

         name = py.token_starts.get((node.lineno, node.col_offset), -1)
         colon = _operator_after(py, name, ':')
         edit = _respace(py, name, colon, 0, 1) if colon >= 0 else None

         if edit:
            yield (node.lineno, node.col_offset + 1,
                   'write `{}: T`'.format(node.arg), [edit])

      for group in _annassign_groups(py):
         yield from _group_sites(py, group)


def _annassign_colon(py, node: ast.AnnAssign) -> Optional[int]:

   end = (node.target.end_lineno, node.target.end_col_offset)
   colon = _operator_after(py, py.token_ends.get(end, -1), ':')
   return colon if colon >= 0 else None


def _annassign_groups(py):
   """Runs of annotated assignments on consecutive lines of one body: a
   run whose annotations start in one column is aligned on purpose, and
   is respaced as a whole."""

   for node in ast.walk(py.tree):
      for body in bodies(node):

         run = []

         for stmt in body:

            ann = isinstance(stmt, ast.AnnAssign)
            one_line = ann and stmt.lineno == stmt.end_lineno

            if run and not (one_line and stmt.lineno == run[-1].lineno + 1):
               yield run
               run = []

            if one_line:
               run.append(stmt)
            elif ann:
               yield [stmt]

         if run:
            yield run


def _group_sites(py, group):

   colons = [_annassign_colon(py, s) for s in group]

   if None in colons:
      return

   cols = {s.annotation.col_offset for s in group}

   if len(group) > 1 and len(cols) == 1:

      # Aligned: the colons go after the names, the types stay in one
      # column, as close to the longest name as `name: T` allows.
      attached = all(py.tokens[c].start == py.tokens[c - 1].end
                     for c in colons)

      if attached:
         return

      col = max(s.target.end_col_offset for s in group) + 2

      for s in group:

         start = s.target.end_col_offset
         pad = ':' + ' ' * (col - start - 1)
         edit = (s.lineno, start, s.annotation.col_offset, pad)

         yield (s.lineno, start + 1, _annassign_message(s), [edit])

      return

   for s, colon in zip(group, colons):

      left = py.token_ends[(s.target.end_lineno, s.target.end_col_offset)]
      edit = _respace(py, left, colon, 0, 1)

      if edit:
         yield (s.lineno, s.target.end_col_offset + 1,
                _annassign_message(s), [edit])


def _annassign_message(node: ast.AnnAssign) -> str:
   return 'write `{}: T`'.format(ast.unparse(node.target))


CALL_KW = PyCallKwSpacing()
DEF_DEFAULT = PyDefDefaultSpacing()
ANNOTATION = PyAnnotationSpacing()
