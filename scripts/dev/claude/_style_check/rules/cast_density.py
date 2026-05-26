# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=consider-using-f-string

import re

from typing import List

from .base import (
   COST_MILD,
   Rule,
   Diagnostic,
   CheckContext,
   LAYER_TOKENS,
   SEVERITY_WARNING,
   SCORE_NUDGE,
)
from .. import tokens as _tokens_mod

# Gradient rule: each C-style cast in a function reduces the
# function's prettiness slightly. More casts = uglier code.
# Not a violation -- only affects the prettiness metric.

# Match C-style casts: `(type *)`, `(const type *)`, `(unsigned type)`,
# `(struct foo *)`, `(enum bar)`, project typedefs (u8..u64, s8..s64,
# ulong, size_t, ssize_t), and plain C types (int, char, void, long,
# short).
_CAST_PAT = re.compile(
   r'\(\s*'
   r'(?:const\s+)?'
   r'(?:unsigned\s+|signed\s+)?'
   r'(?:'
      r'long\s+long'
      r'|long'
      r'|short'
      r'|int'
      r'|char'
      r'|void'
      r'|struct\s+\w+'
      r'|enum\s+\w+'
      r'|u8|u16|u32|u64|ulong'
      r'|s8|s16|s32|s64'
      r'|size_t|ssize_t'
   r')'
   r'\s*\**'
   r'\s*\)'
)

# Match `sizeof(` to skip casts that appear inside sizeof expressions.
_SIZEOF_PAT = re.compile(r'\bsizeof\s*\(')

# A cast must be followed by an expression-starting token (identifier,
# literal, paren, unary operator). This distinguishes real casts from
# type-in-parens that appear in other contexts (e.g. function parameter
# lists like `foo(void)`).
_EXPR_START = re.compile(r'\s*[A-Za-z_0-9(!&*\-~+]')


def _inside_sizeof(masked, pos):
   """Return True if offset `pos` falls inside a sizeof(...) expr."""

   for m in _SIZEOF_PAT.finditer(masked):

      paren_start = m.end() - 1  # position of the `(`
      depth = 1
      i = m.end()

      while i < len(masked) and depth > 0:

         if masked[i] == '(':
            depth += 1
         elif masked[i] == ')':
            depth -= 1

         i += 1

      # pos is inside sizeof if it's between the opening and closing parens
      if paren_start <= pos < i:
         return True

   return False


class CastDensity(Rule):

   id = 'cast_density'
   description = (
      'Each C-style cast in a function reduces prettiness slightly. '
      'Many casts accumulate into a noticeable penalty. '
      '(Gradient rule -- not shown as a violation.)'
   )
   layers = LAYER_TOKENS
   severity = SEVERITY_WARNING
   default_score = SCORE_NUDGE

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      masked = _tokens_mod.mask_non_code(ctx.source_text)
      out = []

      for m in _CAST_PAT.finditer(masked):

         cast_text = m.group(0)
         inner = cast_text.strip().strip('()')

         # Skip bare (void) -- either parameter list or discard pattern
         # (the discard pattern is handled by no_void_cast_discard)
         if re.match(r'^\s*void\s*$', inner):
            continue

         # A real cast must be followed by an expression starter;
         # skip matches that are just types in parentheses in other
         # contexts (e.g. function prototypes, sizeof operands).
         end = m.end()
         rest = masked[end:end + 20] if end < len(masked) else ''

         if not _EXPR_START.match(rest):
            continue

         # Skip casts inside sizeof()
         if _inside_sizeof(masked, m.start()):
            continue

         line, col = _tokens_mod.offset_to_line_col(masked, m.start())
         line_text = ctx.lines[line - 1] \
            if line - 1 < len(ctx.lines) else ''

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=line,
            col=col,
            end_line=line,
            end_col=col + len(cast_text),
            rule=self.id,
            severity=self.severity,
            message='C-style cast `{}`'.format(cast_text.strip()),
            snippet=line_text.strip(),
            is_gradient=True,
            prettiness_cost=COST_MILD,
         ))

      return out


RULE = CastDensity()
