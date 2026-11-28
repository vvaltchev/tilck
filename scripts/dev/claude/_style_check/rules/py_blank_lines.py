# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring

"""
Vertical space in Python, as in the author's C: blank lines delimit groups
of statements, and a block whose body is made of groups is separated from
its header by a blank line too. See docs/plans/python-style.md, 3.1-3.2.
"""

import ast

from typing import List

from .base import (
   CheckContext,
   Diagnostic,
   LAYER_STRUCTURAL,
   SEVERITY_WARNING,
   SCORE_STRONG_PREF,
   SCORE_MEDIUM_PREF,
)
from .py_common import PyRule
from .. import pyctx

COMPOUND = (ast.If, ast.For, ast.AsyncFor, ast.While, ast.With,
            ast.AsyncWith, ast.Try, ast.FunctionDef, ast.AsyncFunctionDef,
            ast.ClassDef)

JUMPS = (ast.Return, ast.Continue, ast.Break, ast.Raise)


class PyBlankAfterJump(PyRule):

   id = 'py_blank_after_jump'
   description = (
      'An `if` ending in return/continue/break/raise is followed by a blank '
      'line when more statements follow'
   )
   layers = LAYER_STRUCTURAL

   def check_py(self, ctx: CheckContext, py) -> List[Diagnostic]:

      out = []

      for node in ast.walk(py.tree):
         for body in _bodies(node):
            for a, b in zip(body, body[1:]):

               if not (isinstance(a, ast.If) and not a.orelse and
                       isinstance(a.body[-1], JUMPS)):

                  continue

               first_b = pyctx.stmt_first_line(b)

               if not any(py.is_blank(n)
                          for n in range(a.end_lineno + 1, first_b)):

                  out.append(self.diag(
                     ctx, first_b, 1,
                     'blank line required after the `if` that ends in `{}` '
                     '(line {})'.format(
                        _jump_word(a.body[-1]), a.lineno
                     )
                  ))

      return out


class PyBlankAfterHeader(PyRule):

   id = 'py_blank_after_header'
   description = (
      'A block whose body is made of groups (blank lines between its '
      'statements) starts after a blank line; a body of one group starts '
      'right after its header'
   )
   layers = LAYER_STRUCTURAL
   severity = SEVERITY_WARNING
   default_score = SCORE_STRONG_PREF

   def check_py(self, ctx: CheckContext, py) -> List[Diagnostic]:

      out = []
      chains = {}
      lines = ctx.lines

      for blk in py.blocks:
         if blk.chain is not None:
            chains.setdefault(blk.chain, []).append(blk)

      siblings = _sibling_defs(py)

      for blk in py.blocks:

         grouped = pyctx.body_is_grouped(py, blk)
         spaced = pyctx.blank_after_header(py, blk)

         # A header spanning several lines is always followed by a blank
         # line, whatever its body: Python's form of C's `{` on its own
         # line after a multi-line condition or signature.
         if not spaced and pyctx.header_spans_lines(py, blk):
            out.append(self.diag(
               ctx, blk.header_line, 1,
               'blank line required after the `{}` header: it spans '
               'several lines'.format(blk.keyword)
            ))
            continue

         if grouped == spaced:
            continue

         if spaced:

            if pyctx.body_is_heavy(py, blk):
               continue

            # A short branch spaced to match a peer, a try chain spaced
            # as a whole, and a short def spaced like its siblings are
            # all part of the style (python-style.md 3.1).
            peers = [p for p in chains.get(blk.chain, []) if p is not blk]

            if any(pyctx.blank_after_header(py, p) for p in peers):
               continue

            if (blk.keyword in ('try', 'except', 'finally') or
                (peers and peers[0].keyword == 'try')):

               continue

            if any(pyctx.blank_after_header(py, sib)
                   for sib in siblings.get(id(blk), [])):

               continue

            out.append(self.diag(
               ctx, blk.header_line + 1, 1,
               'no blank line after the `{}` header: its body is a single '
               'group'.format(blk.keyword)
            ))
            continue

         # Grouped but not spaced: a header shorter than the body's first
         # line does not hide it (contributing.md's rule for C).
         first = pyctx.stmt_first_line(blk.body[0])
         hdr_len = len(lines[blk.header_line - 1].rstrip())
         first_len = len(lines[first - 1].rstrip())

         if hdr_len < first_len:
            continue

         out.append(self.diag(
            ctx, blk.header_line, 1,
            'blank line required after the `{}` header: its body is made '
            'of groups'.format(blk.keyword)
         ))

      return out


def _sibling_defs(py):
   """For each def block, the def blocks next to it in the same body
   (the method before and the one after, in a class)."""

   by_node = {}

   for blk in py.blocks:
      if blk.keyword == 'def':
         by_node[blk.header_line] = blk

   out = {}

   for node in ast.walk(py.tree):
      for body in _bodies(node):

         defs = [d for d in body
                 if isinstance(d, (ast.FunctionDef, ast.AsyncFunctionDef))]
         blks = []

         for d in defs:
            b = next((x for x in by_node.values() if x.body is d.body), None)
            if b is not None:
               blks.append(b)

         for i, b in enumerate(blks):
            out[id(b)] = blks[max(0, i - 1):i] + blks[i + 1:i + 2]

   return out


class PyBranchSymmetry(PyRule):

   id = 'py_branch_symmetry'
   description = (
      'A branch spaced from its header (blank line after it) is also '
      'spaced from the next elif/else/except, and a compact one is not'
   )
   layers = LAYER_STRUCTURAL
   severity = SEVERITY_WARNING
   default_score = SCORE_STRONG_PREF

   def check_py(self, ctx: CheckContext, py) -> List[Diagnostic]:

      out = []
      chains = {}

      for blk in py.blocks:
         if blk.chain is not None:
            chains.setdefault(blk.chain, []).append(blk)

      for blocks in chains.values():

         blocks.sort(key=lambda b: b.index)

         for cur, nxt in zip(blocks, blocks[1:]):

            spaced = pyctx.blank_after_header(py, cur)
            before_next = py.is_blank(nxt.header_line - 1)

            if spaced == before_next:
               continue

            if spaced:
               msg = ('blank line required before `{}`: the `{}` branch '
                      'is spaced from its header')
            else:
               msg = ('no blank line before `{}`: the `{}` branch is not '
                      'spaced from its header')

            msg = msg.format(nxt.keyword, cur.keyword)

            out.append(self.diag(ctx, nxt.header_line, 1, msg))

      return out


class PyBlankBetweenDefs(PyRule):

   id = 'py_blank_between_defs'
   description = (
      'One blank line before a function or class definition (comments '
      'directly above it belong to it)'
   )
   layers = LAYER_STRUCTURAL
   severity = SEVERITY_WARNING
   default_score = SCORE_MEDIUM_PREF

   def check_py(self, ctx: CheckContext, py) -> List[Diagnostic]:

      out = []

      for node in ast.walk(py.tree):
         for body in _bodies(node):
            for prev, d in zip(body, body[1:]):

               if not isinstance(d, (ast.FunctionDef, ast.AsyncFunctionDef,
                                     ast.ClassDef)):

                  continue

               k = pyctx.stmt_first_line(d) - 1

               while (k > prev.end_lineno and
                      ctx.lines[k - 1].lstrip().startswith('#')):

                  k -= 1

               blanks = 0

               while k > prev.end_lineno and py.is_blank(k):
                  blanks += 1
                  k -= 1

               if blanks == 1:
                  continue

               # Two blank lines: a section break, sometimes used before
               # main() or a big function (to be confirmed).
               if blanks == 2 and isinstance(node, ast.Module):
                  continue

               out.append(self.diag(
                  ctx, pyctx.stmt_first_line(d), 1,
                  '{} blank line(s) before `{}`, expected 1'.format(
                     blanks, d.name
                  )
               ))

      return out


class PyNoDoubleBlank(PyRule):

   id = 'py_no_double_blank'
   description = 'No two blank lines in a row inside a function or a class'
   layers = LAYER_STRUCTURAL
   severity = SEVERITY_WARNING
   default_score = SCORE_MEDIUM_PREF

   def check_py(self, ctx: CheckContext, py) -> List[Diagnostic]:

      out = []

      for node in ast.walk(py.tree):

         if not isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef,
                                  ast.ClassDef)):

            continue

         run = 0

         for n in range(node.lineno, node.end_lineno + 1):

            if py.is_blank(n):
               run += 1
               continue

            if run >= 2 and not _starts_def(ctx.lines[n - 1]):
               out.append(self.diag(
                  ctx, n - run, 1,
                  '{} blank lines in a row inside `{}`'.format(run, node.name)
               ))

            run = 0

      # A nested def is visited twice (itself and its parent): keep one.
      seen = set()
      uniq = []

      for d in out:
         if d.line not in seen:
            seen.add(d.line)
            uniq.append(d)

      return uniq


class PyDefDefaultSpacing(PyRule):

   id = 'py_def_default_spacing'
   description = (
      'Parameter defaults in a definition have spaces around `=` '
      '(`delay = 0.2`)'
   )
   layers = LAYER_STRUCTURAL
   severity = SEVERITY_WARNING
   default_score = SCORE_STRONG_PREF

   def check_py(self, ctx: CheckContext, py) -> List[Diagnostic]:

      out = []

      for node in ast.walk(py.tree):

         if not isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef,
                                  ast.Lambda)):

            continue

         if isinstance(node, ast.Lambda):
            continue

         args = node.args
         pos = args.posonlyargs + args.args
         defaults = list(zip(pos[len(pos) - len(args.defaults):],
                             args.defaults))
         defaults += [(a, d) for a, d in zip(args.kwonlyargs,
                                             args.kw_defaults) if d]

         for a, d in defaults:

            line = ctx.lines[d.lineno - 1]

            if d.lineno != (a.end_lineno or a.lineno):
               continue

            start = (a.annotation.end_col_offset if a.annotation
                     else a.col_offset + len(a.arg))
            between = line[start:d.col_offset]

            if between.strip() != '=':
               continue

            if between.startswith(' ') and between.endswith(' '):
               continue

            out.append(self.diag(
               ctx, d.lineno, start + 1,
               'spaces around `=` in the default of `{}`'.format(a.arg)
            ))

      return out


def _bodies(node):

   for field in ('body', 'orelse', 'finalbody'):

      body = getattr(node, field, None)

      if isinstance(body, list) and body and isinstance(body[0], ast.stmt):
         yield body

   for h in getattr(node, 'handlers', []) or []:
      yield h.body


def _starts_def(text: str) -> bool:
   """A line opening a definition, or a comment or decorator above one:
   a blank run before it is py_blank_between_defs' to judge."""

   t = text.lstrip()
   return t.startswith(('def ', 'class ', 'async def ', '@', '#'))


def _jump_word(stmt) -> str:
   return {ast.Return: 'return', ast.Continue: 'continue',
           ast.Break: 'break', ast.Raise: 'raise'}[type(stmt)]


JUMP = PyBlankAfterJump()
HEADER = PyBlankAfterHeader()
SYMMETRY = PyBranchSymmetry()
BETWEEN_DEFS = PyBlankBetweenDefs()
DOUBLE_BLANK = PyNoDoubleBlank()
DEFAULT_SPACING = PyDefDefaultSpacing()
