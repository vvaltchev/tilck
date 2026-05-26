# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=consider-using-f-string

import re

from typing import List

from .base import (
   Rule,
   Diagnostic,
   Fix,
   CheckContext,
   LAYER_RAW_TEXT,
   SEVERITY_WARNING,
   SCORE_MEDIUM_PREF,
)
from .. import tokens as _tokens_mod

# Detect missing blank line before a return statement that is the last
# statement in a function body.
#
# The rule fires when:
#   1. A `return ...;` line is the last statement before the function's
#      closing `}` (only blank lines and `}` lines follow).
#   2. The return is at the function body's top-level indentation (not
#      inside a nested block like an if/else/while/for).
#   3. The line preceding the return (skipping blanks) is not already
#      blank (i.e. there's no breathing room).
#   4. The preceding non-blank line is not `{` (opening brace).
#   5. The preceding non-blank line is not a lock-scope closer like
#      `enable_preemption()` or `enable_interrupts()`.
#   6. The function body has more than 5 statements (small functions
#      are exempt).
#
# This is a text-based rule -- no libclang needed.

_RETURN_LINE = re.compile(r'^\s*return\b')
_RETURN_COMPLEX = re.compile(r'^\s*return\s+.*(?:&&|\|\|)')
_OPEN_BRACE = re.compile(r'^\s*\{\s*$')
_CLOSE_BRACE = re.compile(r'^\s*\}\s*$')
_BLANK_LINE = re.compile(r'^\s*$')

# Lock-scope closing calls: return immediately after these is
# semantically tied to the unlock and doesn't need a blank line.
_UNLOCK_LINE = re.compile(
   r'^\s*enable_preemption\s*\(\s*\)\s*;'
   r'|^\s*enable_interrupts\s*\(\s*\)\s*;'
)

# Goto label: provides visual separation already.
_GOTO_LABEL = re.compile(r'^[a-zA-Z_]\w*:\s*$')

# A "statement line" for counting purposes: non-blank, non-brace-only,
# non-comment-only, non-preprocessor line inside a function body.
_COMMENT_ONLY = re.compile(r'^\s*(/\*.*\*/\s*$|//|\*\s|\*/$|\*$)')
_PP_DIRECTIVE = re.compile(r'^\s*#')


def _find_function_bodies(masked_lines):
   """Yield (body_start, body_end) pairs for top-level function bodies.

   A function body is detected as a `{` at column 0 (no leading
   whitespace) followed eventually by a matching `}` at column 0.
   This is the standard Tilck convention: function-body braces are
   always flush-left.
   """

   i = 0
   n = len(masked_lines)

   while i < n:

      line = masked_lines[i]

      # Function-body opening brace: `{` at column 0
      if line.rstrip() == '{':

         # Find the matching close brace at column 0
         j = i + 1

         while j < n:

            if masked_lines[j].rstrip() == '}':
               break

            j += 1

         # body_start = first line after `{`, body_end = line before `}`
         if j < n:
            yield (i + 1, j - 1)

         i = j + 1

      else:
         i += 1


def _body_indent(masked_lines, start, end):
   """Detect the indentation level of the function body's first
   non-blank statement line. Returns the leading whitespace string."""

   for k in range(start, end + 1):

      line = masked_lines[k]

      if _BLANK_LINE.match(line):
         continue

      if _COMMENT_ONLY.match(line):
         continue

      leading = len(line) - len(line.lstrip())
      return leading

   return 3  # fallback: Tilck convention


def _count_top_level_statements(lines, start, end, indent):
   """Count non-trivial statement lines at the body's top-level
   indentation in the range [start, end]. Lines at deeper indent
   (inside nested blocks) are not counted."""

   count = 0

   for k in range(start, end + 1):

      line = lines[k]

      if _BLANK_LINE.match(line):
         continue

      if _indent_of(line) != indent:
         continue

      if _COMMENT_ONLY.match(line):
         continue

      if _PP_DIRECTIVE.match(line):
         continue

      count += 1

   return count


def _is_last_return_in_body(masked_lines, ret_idx, body_end):
   """Check that after ret_idx, only blank lines and `}` follow
   until body_end (inclusive)."""

   for k in range(ret_idx + 1, body_end + 1):

      line = masked_lines[k]

      if _BLANK_LINE.match(line):
         continue

      if _CLOSE_BRACE.match(line):
         continue

      # Some other statement follows -- this return is not the last
      return False

   return True


def _prev_nonblank_idx(lines, idx):
   """Return the index of the previous non-blank line, or -1."""

   k = idx - 1

   while k >= 0:

      if not _BLANK_LINE.match(lines[k]):
         return k

      k -= 1

   return -1


def _indent_of(line):
   """Return the number of leading spaces on a line."""

   return len(line) - len(line.lstrip())


def _has_label_before(lines, ret_idx, body_start):
   """Return True if a goto label appears between the return and
   the nearest preceding blank line (or body start). This covers
   cleanup blocks like: `errend: free_foo(); return rc;`"""

   k = ret_idx - 1

   while k >= body_start:

      if _BLANK_LINE.match(lines[k]):
         return False  # hit a blank line without finding a label

      if _GOTO_LABEL.match(lines[k]):
         return True

      k -= 1

   return False


class BlankLineBeforeReturn(Rule):

   id = 'blank_line_before_return'
   description = (
      'When a `return` is the last statement in a function body, '
      'there should be a blank line before it for visual breathing '
      'room. Exempt: trivial functions (<= 5 statements), returns '
      'inside nested blocks, returns after lock-scope closers, and '
      'returns whose preceding line is `{`.'
   )
   layers = LAYER_RAW_TEXT
   severity = SEVERITY_WARNING
   default_score = SCORE_MEDIUM_PREF

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      masked = _tokens_mod.mask_non_code(ctx.source_text)
      masked_lines = masked.split('\n')
      out = []

      for body_start, body_end in _find_function_bodies(masked_lines):

         # Determine this body's top-level indentation
         top_indent = _body_indent(masked_lines, body_start, body_end)

         # Find the last return in this function body
         last_return_idx = -1

         for k in range(body_start, body_end + 1):

            if not _RETURN_LINE.match(masked_lines[k]):
               continue

            # Must be at the body's top-level indentation
            if _indent_of(masked_lines[k]) != top_indent:
               continue

            if _is_last_return_in_body(masked_lines, k, body_end):
               last_return_idx = k
               break  # first match that is also last stmt

         if last_return_idx < 0:
            continue

         # Exception: trivially small function bodies
         stmt_count = _count_top_level_statements(
            masked_lines, body_start, body_end, top_indent
         )

         if stmt_count <= 3:
            continue

         # Check the line before the return (skipping blanks)
         prev_idx = _prev_nonblank_idx(masked_lines, last_return_idx)

         if prev_idx < 0:
            continue

         prev_line = masked_lines[prev_idx]

         # Exception: previous non-blank line is `{`
         if _OPEN_BRACE.match(prev_line):
            continue

         # Exception: previous line is a lock-scope closer
         if _UNLOCK_LINE.match(prev_line):
            continue

         # Exception: previous non-blank line is a goto label
         if _GOTO_LABEL.match(prev_line):
            continue

         # Exception: a goto label appears between the return and the
         # nearest preceding blank line (cleanup blocks after labels
         # don't need additional blank-line separation)
         if _has_label_before(masked_lines, last_return_idx, body_start):
            continue

         # Only flag complex return expressions (containing && or ||).
         # Simple returns (return var; return 0; return true;) don't
         # need the blank line — the visual separation between "do
         # stuff" and "return result" is already clear.
         if not _RETURN_COMPLEX.match(masked_lines[last_return_idx]):
            continue

         # Check if there's already a blank line before the return
         if last_return_idx > 0:

            line_before = masked_lines[last_return_idx - 1]

            if _BLANK_LINE.match(line_before):
               continue  # already has a blank line

         # Check for a blank line 2 lines before the return: if
         # line N-2 is blank, lines N-1 and N form a semantic group
         # (e.g. assignment + return) that already has breathing room
         # from the preceding code.
         if last_return_idx > 1:

            two_before = masked_lines[last_return_idx - 2]

            if _BLANK_LINE.match(two_before):
               continue

         line_no = last_return_idx + 1  # 1-based
         line_text = ctx.lines[last_return_idx] \
            if last_return_idx < len(ctx.lines) else ''

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=line_no,
            col=1,
            end_line=line_no,
            end_col=len(line_text) + 1,
            rule=self.id,
            severity=self.severity,
            message=(
               'missing blank line before final `return` in function '
               'body -- add a blank line for visual breathing room'
            ),
            snippet=line_text.strip(),
            suggestion='add a blank line before this return',
            fixes=[Fix(line_no, line_no,
                        ['', line_text.rstrip()],
                        'insert blank line before return')],
         ))

      return out


RULE = BlankLineBeforeReturn()
