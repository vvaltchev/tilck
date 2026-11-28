# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=missing-function-docstring, bad-indentation
# pylint: disable=too-many-instance-attributes, too-few-public-methods
"""
What the Python rules read: the tokens, the syntax tree, the brackets, and
the blocks with their bodies.

Python needs no build and no libclang: the standard library's tokenize and
ast modules parse any Python file on any host. A file they reject has no
context (`PyContext.error` says why), and that is reported as a parse error,
never skipped silently.
"""

import io
import ast
import tokenize

from dataclasses import dataclass, field
from typing import Dict, List, Optional, Set


@dataclass
class Bracket:
   """A multi-line bracket pair."""

   char: str                  # '(', '[' or '{'
   open_line: int             # 1-based
   open_col: int              # 0-based
   close_line: int
   close_col: int
   open_line_indent: int      # indentation of the line the bracket opens on
   hanging: bool              # the bracket ends its line
   first_col: Optional[int]   # column of the first element, when not hanging
   close_alone: bool          # the closing bracket starts its line

   # (line, col) of each line start at depth 1 inside the bracket, and
   # whether it starts a new element (the previous line ended with `,`,
   # or it is the first line after the opening one).
   line_starts: List[tuple] = field(default_factory=list)


@dataclass
class Block:
   """A compound statement's header and body, or one branch of an
   if/elif/else or try/except chain."""

   keyword: str               # def, class, for, while, with, if, elif,
                              # else, try, except, finally
   header_line: int           # the line of the header's colon
   body: list                 # the body's statements (ast nodes)
   chain: Optional[int] = None    # id shared by the branches of a chain
   index: int = 0                 # position in the chain


@dataclass
class PyContext:

   tokens: list
   tree: Optional[ast.AST]
   error: Optional[str]
   lines: List[str]
   brackets: List[Bracket] = field(default_factory=list)
   blocks: List[Block] = field(default_factory=list)

   # the lines inside a string literal spanning several lines, all but its
   # first: their text is the string's, blank or not
   string_lines: Set[int] = field(default_factory=set)

   # line -> indentation and first line of the logical line it belongs to
   logical_indent: Dict[int, int] = field(default_factory=dict)
   logical_start: Dict[int, int] = field(default_factory=dict)

   # (line, col) where a code token starts or ends -> its index in `tokens`
   token_starts: Dict[tuple, int] = field(default_factory=dict)
   token_ends: Dict[tuple, int] = field(default_factory=dict)

   # where Python's parser stopped, when `error` is set (0: unknown)
   error_line: int = 0

   def is_blank(self, n: int) -> bool:
      """Whether line n is a blank line of code: empty, and not inside a
      string literal."""

      if not 0 < n <= len(self.lines) or n in self.string_lines:
         return False

      return not self.lines[n - 1].strip()

   def next_code(self, i: int) -> int:
      """The index of the code token after tokens[i] (line breaks and
      comments inside brackets skipped), or -1."""

      for j in range(i + 1, len(self.tokens)):
         if self.tokens[j].type not in _NOT_CODE:
            return j

      return -1


def build(source_text: str) -> PyContext:

   lines = source_text.split('\n')

   try:
      tree = ast.parse(source_text)
      tokens = list(tokenize.generate_tokens(io.StringIO(source_text).readline))
   except SyntaxError as e:
      return PyContext([], None, 'SyntaxError: {}'.format(e.msg), lines,
                       error_line=e.lineno or 0)
   except tokenize.TokenError as e:
      return PyContext([], None, 'TokenError: {}'.format(e.args[0]), lines,
                       error_line=e.args[1][0])

   ctx = PyContext(tokens, tree, None, lines)
   _index_tokens(ctx)
   _find_logical_lines(ctx)
   _find_string_lines(ctx)
   _find_brackets(ctx)
   _find_blocks(ctx)
   return ctx


def _indent_of(line: str) -> int:
   return len(line) - len(line.lstrip(' '))

# Tokens that carry no code: a rule stepping from one token to the next
# skips them.
_NOT_CODE = (tokenize.NL, tokenize.NEWLINE, tokenize.COMMENT, tokenize.INDENT,
             tokenize.DEDENT, tokenize.ENCODING, tokenize.ENDMARKER)


def _index_tokens(ctx: PyContext):

   for i, t in enumerate(ctx.tokens):
      if t.type not in _NOT_CODE:
         ctx.token_starts[t.start] = i
         ctx.token_ends[t.end] = i


def _find_string_lines(ctx: PyContext):

   # Python 3.12+ tokenizes an f-string as FSTRING_START ... FSTRING_END,
   # nesting; older versions as one STRING token.
   fstring_start = getattr(tokenize, 'FSTRING_START', None)
   fstring_end = getattr(tokenize, 'FSTRING_END', None)
   depth = 0
   first = 0

   for t in ctx.tokens:

      if t.type == tokenize.STRING and depth == 0:

         ctx.string_lines.update(range(t.start[0] + 1, t.end[0] + 1))

      elif t.type == fstring_start:

         if depth == 0:
            first = t.start[0]

         depth += 1

      elif t.type == fstring_end:

         depth -= 1

         if depth == 0:
            ctx.string_lines.update(range(first + 1, t.end[0] + 1))


def _find_logical_lines(ctx: PyContext):

   start = None

   for t in ctx.tokens:

      if t.type in (tokenize.NL, tokenize.COMMENT, tokenize.INDENT,
                    tokenize.DEDENT, tokenize.ENCODING):

         continue

      if start is None:
         start = t.start[0]
         indent = _indent_of(ctx.lines[start - 1])

      for ln in range(t.start[0], t.end[0] + 1):
         ctx.logical_indent.setdefault(ln, indent)
         ctx.logical_start.setdefault(ln, start)

      if t.type in (tokenize.NEWLINE, tokenize.ENDMARKER):
         start = None


def _find_brackets(ctx: PyContext):

   toks = [t for t in ctx.tokens if t.type != tokenize.ENCODING]
   stack = []

   for i, t in enumerate(toks):

      if t.type != tokenize.OP:
         continue

      if t.string in '([{':
         stack.append(i)
         continue

      if t.string not in ')]}' or not stack:
         continue

      i0 = stack.pop()
      o = toks[i0]

      if o.start[0] == t.start[0]:
         continue

      nxt = toks[i0 + 1]
      hanging = nxt.type in (tokenize.NL, tokenize.COMMENT)
      close_text = ctx.lines[t.start[0] - 1]

      b = Bracket(
         char=o.string,
         open_line=o.start[0],
         open_col=o.start[1],
         close_line=t.start[0],
         close_col=t.start[1],
         open_line_indent=_indent_of(ctx.lines[o.start[0] - 1]),
         hanging=hanging,
         first_col=None if hanging else nxt.start[1],
         close_alone=close_text[:t.start[1]].strip() == '',
      )

      # The first token of each physical line inside the bracket, at
      # depth 1 (directly inside it), including its own closing bracket.
      depth = 0
      prev_sig = o
      last_end = o.end[0]

      for j in range(i0, i + 1):

         tj = toks[j]

         if tj.type in (tokenize.NL, tokenize.NEWLINE):
            continue

         if j > i0 and tj.start[0] > last_end and depth == 1:
            new_elem = prev_sig is o or prev_sig.string == ','
            b.line_starts.append((tj.start[0], tj.start[1], new_elem))

         if tj.type == tokenize.OP and tj.string in '([{':
            depth += 1
         elif tj.type == tokenize.OP and tj.string in ')]}':
            depth -= 1

         if tj.type != tokenize.COMMENT:
            prev_sig = tj

         last_end = max(last_end, tj.end[0])

      ctx.brackets.append(b)


_CHAIN_KINDS = {'elif', 'else', 'except', 'finally'}


def _find_blocks(ctx: PyContext):

   chain_id = [0]

   def header_line_before(body_first: int) -> int:
      """The line of the header's ':' -- the last code line before
      the body, skipping blank lines and comments."""

      k = body_first - 1

      while k >= 1:
         s = ctx.lines[k - 1].strip()
         if s and not s.startswith('#'):
            return k

         k -= 1

      return k

   def first_line(stmt) -> int:
      if getattr(stmt, 'decorator_list', None):
         return stmt.decorator_list[0].lineno

      return stmt.lineno

   def add(keyword, body, chain=None, index=0):

      if not body:
         return

      first = first_line(body[0])
      col = body[0].col_offset

      # A body on its header's line (`if x: return`) has no header line
      # of its own to be spaced from.
      if ctx.lines[first - 1][:col].strip():
         return

      hl = header_line_before(first)
      ctx.blocks.append(Block(keyword, hl, body, chain, index))

   for node in ast.walk(ctx.tree):

      if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
         add('def', node.body)
      elif isinstance(node, ast.ClassDef):
         add('class', node.body)
      elif isinstance(node, (ast.For, ast.AsyncFor, ast.While)):
         add('for' if not isinstance(node, ast.While) else 'while',
             node.body)
         if node.orelse:
            add('else', node.orelse)
      elif isinstance(node, (ast.With, ast.AsyncWith)):
         add('with', node.body)
      elif isinstance(node, ast.If):

         if _is_elif(ctx, node):
            continue  # part of the chain of its first `if`

         chain_id[0] += 1
         cid = chain_id[0]
         idx = 0
         add('if', node.body, cid, idx)
         cur = node

         while cur.orelse:

            o = cur.orelse[0]
            idx += 1

            if (len(cur.orelse) == 1 and isinstance(o, ast.If) and
                _is_elif(ctx, o)):

               add('elif', o.body, cid, idx)
               cur = o
               continue

            add('else', cur.orelse, cid, idx)
            break

      elif isinstance(node, ast.Try):

         chain_id[0] += 1
         cid = chain_id[0]
         idx = 0
         add('try', node.body, cid, idx)

         for h in node.handlers:
            idx += 1
            add('except', h.body, cid, idx)

         if node.orelse:
            idx += 1
            add('else', node.orelse, cid, idx)

         if node.finalbody:
            idx += 1
            add('finally', node.finalbody, cid, idx)


def _is_elif(ctx: PyContext, node: ast.If) -> bool:
   return ctx.lines[node.lineno - 1].lstrip().startswith('elif')


def stmt_first_line(stmt) -> int:
   if getattr(stmt, 'decorator_list', None):
      return stmt.decorator_list[0].lineno

   return stmt.lineno


def body_is_grouped(py: PyContext, block: Block) -> bool:
   """Whether blank lines split the body into groups: any blank line
   between the body's first line and its last statement, except the ones
   right after the header (the spacing being judged) and the ones inside
   nested blocks (those split the nested body, not this one). A blank line
   between a leading comment and the first statement counts."""

   body = block.body
   nested = set()

   for stmt in body:
      if hasattr(stmt, 'body') and stmt.end_lineno > stmt.lineno:
         nested.update(range(stmt.lineno + 1, stmt.end_lineno + 1))

   n = block.header_line + 1

   while py.is_blank(n):
      n += 1

   for i in range(n, body[-1].end_lineno + 1):
      if i not in nested and py.is_blank(i):
         return True

   return False


def body_is_heavy(py: PyContext, block: Block) -> bool:
   """A body that may be spaced from its header without being grouped:
   one compound statement, a statement spanning several lines, a leading
   comment, or a header that itself spans several lines."""

   lines = py.lines
   body = block.body
   first = stmt_first_line(body[0])

   if len(body) == 1 and hasattr(body[0], 'body'):
      return True

   if any(s.end_lineno > stmt_first_line(s) for s in body):
      return True

   k = block.header_line + 1

   while k < first:
      if lines[k - 1].strip().startswith('#'):
         return True

      k += 1

   return header_spans_lines(py, block)


def header_spans_lines(py: PyContext, block: Block) -> bool:
   """Whether the header spans several lines (a multi-line `def (...)` or
   `if (...)`): the logical line ending in the header's colon starts on an
   earlier line."""

   return py.logical_start.get(block.header_line) != block.header_line


def blank_after_header(py: PyContext, block: Block) -> bool:
   """Whether a blank line follows the header (comments between the
   header and the first statement belong to the body)."""

   return py.is_blank(block.header_line + 1)
