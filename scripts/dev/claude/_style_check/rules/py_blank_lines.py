# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring

"""
Vertical space in Python, as in the author's C: blank lines delimit groups
of statements, and a block whose body is made of groups is separated from
its header by a blank line too. See docs/plans/python-style.md, 3.1-3.2.

Vertical space is weight: the more structure a neighbour has inside, the
more space may separate it; a blank line needs a reason; a light block
cannot carry extra air; the spacing tells the common path from the rare
one; and the emphasis is the writer's call. A script cannot weigh that, so
every rule here is soft: a finding is a question about the code, to be
judged in context by reading it (by Claude, or a person), never applied
blindly. Each finding says so in its suggestion.
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
   SCORE_NUDGE,
)
from .py_common import PyRule, bodies
from .. import pyctx

JUDGE = ('soft: judge it in context -- the weight and density of the '
         'blocks around, the common path, the emphasis '
         '(docs/plans/python-style.md 3.1)')

# A body of this many code lines or fewer is too light to need a blank
# line after its header, groups or not (count_lines_of_code:124).
SMALL_BODY_LINES = 4

# Two or three blank lines are a section break; more are never seen.
MAX_BLANK_RUN = 3

COMPOUND = (ast.If, ast.For, ast.AsyncFor, ast.While, ast.With,
            ast.AsyncWith, ast.Try, ast.FunctionDef, ast.AsyncFunctionDef,
            ast.ClassDef)

JUMPS = (ast.Return, ast.Continue, ast.Break, ast.Raise)


class _BlankRule(PyRule):
   """A soft rule about blank lines: its findings are judged, not
   applied (the module's docstring)."""

   layers = LAYER_STRUCTURAL
   severity = SEVERITY_WARNING
   default_score = SCORE_MEDIUM_PREF

   def diag(self, ctx, line, col, message, suggestion=None, end_line=None):
      return super().diag(ctx, line, col, message, suggestion or JUDGE,
                          end_line)


class PyBlankAfterJump(_BlankRule):

   id = 'py_blank_after_jump'
   description = (
      'An `if` ending in return/continue/break/raise is followed by a blank '
      'line when more statements follow (93/93 in the author\'s code: the '
      'heaviest of the soft blank-line rules)'
   )
   default_score = SCORE_STRONG_PREF

   def check_py(self, ctx: CheckContext, py) -> List[Diagnostic]:

      out = []

      for node in ast.walk(py.tree):
         for body in bodies(node):
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


class PyBlankAfterHeader(_BlankRule):

   id = 'py_blank_after_header'
   description = (
      'A block whose body is made of groups (blank lines between its '
      'statements) starts after a blank line; a body of one group starts '
      'right after its header. A header spanning lines is always followed '
      'by one; a small body (4 code lines or fewer) may go either way. '
      'Judged in context: a group of prints may be the title of what '
      'follows, and needs no blank line above it'
   )

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

         # Grouped but not spaced: a small body is too light to need the
         # blank line, and a header shorter than the body's first line
         # does not hide it (contributing.md's rule for C).
         if _code_lines(py, blk) <= SMALL_BODY_LINES:
            continue

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


def _code_lines(py, blk) -> int:
   """The lines of a body holding code: not blank, not only a comment."""

   first = pyctx.stmt_first_line(blk.body[0])
   last = blk.body[-1].end_lineno

   return sum(1 for n in range(first, last + 1)
              if not py.is_blank(n) and
              not py.lines[n - 1].lstrip().startswith('#'))


def _sibling_defs(py):
   """For each def block, the def blocks next to it in the same body
   (the method before and the one after, in a class)."""

   by_node = {}

   for blk in py.blocks:
      if blk.keyword == 'def':
         by_node[blk.header_line] = blk

   out = {}

   for node in ast.walk(py.tree):
      for body in bodies(node):

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


class PyBranchSymmetry(_BlankRule):

   id = 'py_branch_symmetry'
   description = (
      'A hint: a branch spaced from its header (blank line after it) is '
      'often spaced from the next elif/else/except too, and a compact one '
      'is not. Symmetric spacing says "equally likely alternatives", which '
      'is rare: a light common path flowing into the else of a dense, rare '
      'one is the style (regs_printer.py:64-77)'
   )
   default_score = SCORE_NUDGE

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


class PyBlankBetweenDefs(_BlankRule):

   id = 'py_blank_between_defs'
   description = (
      'A function or class definition is set apart from the statement '
      'before it by a blank line (comments directly above it belong to it). '
      'How many is py_blank_line_runs\' to judge'
   )

   def check_py(self, ctx: CheckContext, py) -> List[Diagnostic]:

      out = []

      for node in ast.walk(py.tree):
         for body in bodies(node):
            for prev, d in zip(body, body[1:]):

               if not isinstance(d, (ast.FunctionDef, ast.AsyncFunctionDef,
                                     ast.ClassDef)):

                  continue

               k = pyctx.stmt_first_line(d) - 1

               while (k > prev.end_lineno and
                      ctx.lines[k - 1].lstrip().startswith('#')):

                  k -= 1

               if k > prev.end_lineno and py.is_blank(k):
                  continue

               out.append(self.diag(
                  ctx, pyctx.stmt_first_line(d), 1,
                  'no blank line before `{}`'.format(d.name)
               ))

      return out


class PyBlankLineRuns(_BlankRule):

   id = 'py_blank_line_runs'
   description = (
      'Two or three blank lines in a row are a section break, one level '
      'above the single blank lines: one of the sections they separate has '
      'blank lines of its own (a run of groups inside a function, a big '
      'method, a class, the helpers before main()). With no groups around, '
      'or four or more in a row, they are a slip'
   )

   def check_py(self, ctx: CheckContext, py) -> List[Diagnostic]:

      out = []
      total = len(py.lines)
      n = 1

      while n <= total:

         if not py.is_blank(n):
            n += 1
            continue

         start = n

         while py.is_blank(n):
            n += 1

         run = n - start

         # Blank lines at the top or the end of the file separate nothing.
         if run < 2 or start == 1 or n > total:
            continue

         if run > MAX_BLANK_RUN:
            out.append(self.diag(
               ctx, start, 1,
               '{} blank lines in a row: a section break takes 2 or 3'
               .format(run)
            ))
            continue

         level = _indent(py, n)

         if (_has_groups(py, start - 1, -1, run, level) or
             _has_groups(py, n, 1, run, level)):

            continue

         out.append(self.diag(
            ctx, start, 1,
            '{} blank lines in a row, with no groups around them: neither '
            'side has blank lines of its own'.format(run)
         ))

      return out


def _indent(py, n: int) -> int:
   """The indentation of the statement line n belongs to (a comment line:
   its own)."""

   text = py.lines[n - 1]
   return py.logical_indent.get(n, len(text) - len(text.lstrip(' ')))


def _has_groups(py, n: int, step: int, run: int, level: int) -> bool:
   """Whether the section starting at line n and walking by `step` has
   blank lines of its own. The section ends at a run of `run` blank lines
   or more, or at a line indented less than `level` (it left the block)."""

   blanks = 0
   groups = False

   while 1 <= n <= len(py.lines):

      if py.is_blank(n):
         blanks += 1
         n += step
         continue

      if blanks >= run or _indent(py, n) < level:
         break

      groups = groups or blanks > 0
      blanks = 0
      n += step

   return groups


def _jump_word(stmt) -> str:
   return {ast.Return: 'return', ast.Continue: 'continue',
           ast.Break: 'break', ast.Raise: 'raise'}[type(stmt)]

JUMP = PyBlankAfterJump()
HEADER = PyBlankAfterHeader()
SYMMETRY = PyBranchSymmetry()
BETWEEN_DEFS = PyBlankBetweenDefs()
BLANK_RUNS = PyBlankLineRuns()
