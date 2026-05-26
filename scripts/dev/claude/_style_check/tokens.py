# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=missing-module-docstring, missing-class-docstring
# pylint: disable=missing-function-docstring

from dataclasses import dataclass
from typing import List


@dataclass
class CommentRange:

   kind: str  # 'block' for /* */, 'line' for //
   start_line: int  # 1-based
   start_col: int   # 1-based
   end_line: int
   end_col: int    # column AFTER the last char (exclusive)
   text: str


# State machine: walks the source byte-by-byte, tracking whether we're
# inside a string literal, char literal, or comment. This is the
# authoritative comment scanner -- the libclang token API can miss
# comments inside macro bodies or after line continuations.

_S_CODE = 0       # default
_S_LINE_COMMENT = 1
_S_BLOCK_COMMENT = 2
_S_STRING = 3
_S_CHAR = 4


def scan_comments(source: str) -> List[CommentRange]:

   out = []
   state = _S_CODE
   line = 1
   col = 1
   start_line = 0
   start_col = 0
   start_idx = 0

   i = 0
   n = len(source)

   while i < n:

      c = source[i]
      nxt = source[i + 1] if i + 1 < n else ''

      if state == _S_CODE:

         if c == '/' and nxt == '/':
            state = _S_LINE_COMMENT
            start_line = line
            start_col = col
            start_idx = i
            i += 2
            col += 2
            continue

         if c == '/' and nxt == '*':
            state = _S_BLOCK_COMMENT
            start_line = line
            start_col = col
            start_idx = i
            i += 2
            col += 2
            continue

         if c == '"':
            state = _S_STRING
            i += 1
            col += 1
            continue

         if c == "'":
            state = _S_CHAR
            i += 1
            col += 1
            continue

         if c == '\n':
            line += 1
            col = 1
            i += 1
            continue

         col += 1
         i += 1
         continue

      if state == _S_LINE_COMMENT:

         # Terminates at end of line, BUT a backslash-newline continues
         # the comment onto the next line.
         if c == '\n':

            if i > 0 and source[i - 1] == '\\':
               # Line continuation: comment continues.
               line += 1
               col = 1
               i += 1
               continue

            out.append(CommentRange(
               kind='line',
               start_line=start_line,
               start_col=start_col,
               end_line=line,
               end_col=col,
               text=source[start_idx:i],
            ))
            state = _S_CODE
            line += 1
            col = 1
            i += 1
            continue

         col += 1
         i += 1
         continue

      if state == _S_BLOCK_COMMENT:

         if c == '*' and nxt == '/':
            out.append(CommentRange(
               kind='block',
               start_line=start_line,
               start_col=start_col,
               end_line=line,
               end_col=col + 2,
               text=source[start_idx:i + 2],
            ))
            state = _S_CODE
            i += 2
            col += 2
            continue

         if c == '\n':
            line += 1
            col = 1
            i += 1
            continue

         col += 1
         i += 1
         continue

      if state == _S_STRING:

         if c == '\\' and nxt:
            # Skip escape sequence
            i += 2
            col += 2
            continue

         if c == '"':
            state = _S_CODE
            i += 1
            col += 1
            continue

         if c == '\n':
            line += 1
            col = 1
            i += 1
            continue

         col += 1
         i += 1
         continue

      if state == _S_CHAR:

         if c == '\\' and nxt:
            i += 2
            col += 2
            continue

         if c == "'":
            state = _S_CODE
            i += 1
            col += 1
            continue

         if c == '\n':
            line += 1
            col = 1
            i += 1
            continue

         col += 1
         i += 1
         continue

   # EOF inside a comment -- close it.
   if state == _S_BLOCK_COMMENT or state == _S_LINE_COMMENT:

      out.append(CommentRange(
         kind='block' if state == _S_BLOCK_COMMENT else 'line',
         start_line=start_line,
         start_col=start_col,
         end_line=line,
         end_col=col,
         text=source[start_idx:],
      ))

   return out


def mask_non_code(source: str) -> str:
   """Return source with all comment / string-literal / char-literal
   content replaced by spaces, preserving line breaks and original
   indexing. Useful for regex-style code-only matching."""

   out = []
   state = _S_CODE
   i = 0
   n = len(source)

   while i < n:

      c = source[i]
      nxt = source[i + 1] if i + 1 < n else ''

      if state == _S_CODE:

         if c == '/' and nxt == '/':
            state = _S_LINE_COMMENT
            out.append(' ')
            out.append(' ')
            i += 2
            continue

         if c == '/' and nxt == '*':
            state = _S_BLOCK_COMMENT
            out.append(' ')
            out.append(' ')
            i += 2
            continue

         if c == '"':
            state = _S_STRING
            out.append(' ')
            i += 1
            continue

         if c == "'":
            state = _S_CHAR
            out.append(' ')
            i += 1
            continue

         out.append(c)
         i += 1
         continue

      if state == _S_LINE_COMMENT:

         if c == '\n':

            if i > 0 and source[i - 1] == '\\':
               out.append('\n')
               i += 1
               continue

            out.append('\n')
            state = _S_CODE
            i += 1
            continue

         out.append(' ')
         i += 1
         continue

      if state == _S_BLOCK_COMMENT:

         if c == '*' and nxt == '/':
            out.append(' ')
            out.append(' ')
            state = _S_CODE
            i += 2
            continue

         if c == '\n':
            out.append('\n')
            i += 1
            continue

         out.append(' ')
         i += 1
         continue

      if state == _S_STRING:

         if c == '\\' and nxt:
            out.append(' ')
            out.append(' ')
            i += 2
            continue

         if c == '"':
            out.append(' ')
            state = _S_CODE
            i += 1
            continue

         if c == '\n':
            out.append('\n')
            i += 1
            continue

         out.append(' ')
         i += 1
         continue

      if state == _S_CHAR:

         if c == '\\' and nxt:
            out.append(' ')
            out.append(' ')
            i += 2
            continue

         if c == "'":
            out.append(' ')
            state = _S_CODE
            i += 1
            continue

         if c == '\n':
            out.append('\n')
            i += 1
            continue

         out.append(' ')
         i += 1
         continue

   return ''.join(out)


def offset_to_line_col(source: str, offset: int) -> tuple:
   """Convert a string offset to a (line, col) 1-based pair."""

   line = source.count('\n', 0, offset) + 1
   last_nl = source.rfind('\n', 0, offset)
   col = offset - last_nl  # if last_nl == -1, this is offset + 1

   return line, col


def line_col_to_offset(source: str, line: int, col: int) -> int:
   """Convert a 1-based (line, col) pair to a string offset, or -1
   if out of range."""

   if line < 1 or col < 1:
      return -1

   offset = 0
   target = line - 1

   for _ in range(target):

      nl = source.find('\n', offset)

      if nl < 0:
         return -1

      offset = nl + 1

   return offset + col - 1


def find_matching_close(source: str, open_offset: int,
                        open_ch: str = '(',
                        close_ch: str = ')') -> int:
   """Given an offset pointing at an opening brace/paren, return the
   offset of the matching closer, or -1. Skips strings, char literals,
   and comments correctly via the same state machine as scan_comments."""

   if open_offset >= len(source) or source[open_offset] != open_ch:
      return -1

   state = _S_CODE
   depth = 0
   i = open_offset
   n = len(source)

   while i < n:

      c = source[i]
      nxt = source[i + 1] if i + 1 < n else ''

      if state == _S_CODE:

         if c == '/' and nxt == '/':
            state = _S_LINE_COMMENT
            i += 2
            continue

         if c == '/' and nxt == '*':
            state = _S_BLOCK_COMMENT
            i += 2
            continue

         if c == '"':
            state = _S_STRING
            i += 1
            continue

         if c == "'":
            state = _S_CHAR
            i += 1
            continue

         if c == open_ch:
            depth += 1
            i += 1
            continue

         if c == close_ch:
            depth -= 1

            if depth == 0:
               return i

            i += 1
            continue

         i += 1
         continue

      if state == _S_LINE_COMMENT:

         if c == '\n':
            state = _S_CODE

         i += 1
         continue

      if state == _S_BLOCK_COMMENT:

         if c == '*' and nxt == '/':
            state = _S_CODE
            i += 2
            continue

         i += 1
         continue

      if state == _S_STRING:

         if c == '\\' and nxt:
            i += 2
            continue

         if c == '"':
            state = _S_CODE

         i += 1
         continue

      if state == _S_CHAR:

         if c == '\\' and nxt:
            i += 2
            continue

         if c == "'":
            state = _S_CODE

         i += 1
         continue

   return -1


def build_line_in_comment_index(comments: List[CommentRange],
                                num_lines: int) -> List[bool]:
   """Return a (num_lines+1)-element list where index L is True iff line
   L is entirely (or starts) inside a comment. Useful for rules that
   want to skip code that lives inside a comment."""

   marks = [False] * (num_lines + 2)

   for cr in comments:

      for ln in range(cr.start_line, cr.end_line + 1):

         if 1 <= ln < len(marks):
            marks[ln] = True

   return marks


def is_inside_comment(comments: List[CommentRange],
                      line: int,
                      col: int) -> bool:

   for cr in comments:

      if cr.start_line < line < cr.end_line:
         return True

      if cr.start_line == line and cr.start_col <= col:
         if cr.end_line > line or col < cr.end_col:
            return True

      if cr.end_line == line and col < cr.end_col:
         if cr.start_line < line or cr.start_col <= col:
            return True

   return False
