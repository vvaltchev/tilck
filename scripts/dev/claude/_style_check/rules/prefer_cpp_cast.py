# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring

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

# C-style cast pattern: (Type)expr or (Type *)expr.
# Reuses the general shape from cast_no_asymmetric_form but broader:
# any `(SomeType)` or `(SomeType *)` followed by an expression starter.
# Excludes common false positives: sizeof(type), return(val), if(cond),
# while(cond), switch(cond), and function calls (identifier immediately
# before the paren).
_C_CAST_PAT = re.compile(
   r'(?<![A-Za-z_\d])'           # not preceded by identifier char
   r'\('
   r'[A-Za-z_][\w\s]*'           # type name (may include spaces)
   r'(?:\s*\*+)?'                # optional pointer stars
   r'\s*\)'
   r'\s*'
   r'[A-Za-z_\d(!&*~\-+]'       # expression starter
)

# Keywords whose parens look like casts but are not.
_KEYWORDS = {
   'sizeof', 'typeof', 'alignof', '_Alignof', '__typeof__',
   'return', 'if', 'while', 'for', 'switch', 'case',
}

# Pure type keywords that appear as `(int)x` etc.
_TYPE_KW = {
   'void', 'char', 'short', 'int', 'long', 'float', 'double',
   'signed', 'unsigned', 'bool', 'size_t', 'ssize_t', 'u8', 'u16',
   'u32', 'u64', 's8', 's16', 's32', 's64', 'ulong', 'offt',
   'uint', 'uptr',
}


class PreferCppCast(Rule):

   id = 'prefer_cpp_cast'
   description = (
      'Prefer C++ casts (static_cast<T>(x)) over C-style casts '
      '((T)x) in .cpp files'
   )
   layers = LAYER_TOKENS
   severity = SEVERITY_WARNING
   default_score = SCORE_NUDGE
   applies_to = {'.cpp', '.hpp'}

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      if not ctx.is_cpp:
         return []

      masked = _tokens_mod.mask_non_code(ctx.source_text)
      out = []

      for m in _C_CAST_PAT.finditer(masked):

         # Extract the token before the opening paren to rule out
         # function calls and keyword parens.
         start = m.start()

         # Walk backward over whitespace to find preceding token
         pre_end = start
         pre_start = pre_end

         while pre_start > 0 and masked[pre_start - 1].isalnum():
            pre_start -= 1

         while pre_start > 0 and masked[pre_start - 1] == '_':
            pre_start -= 1

         while pre_start > 0 and masked[pre_start - 1].isalnum():
            pre_start -= 1

         if pre_start < pre_end:
            preceding = masked[pre_start:pre_end]
            if preceding in _KEYWORDS:
               continue

         # Extract the type inside parens
         paren_content = m.group(0)
         open_idx = paren_content.index('(')
         close_idx = paren_content.index(')')
         cast_type = paren_content[open_idx + 1:close_idx].strip()

         # Skip if it looks like a function pointer cast or compound
         # literal: (struct foo){...}
         if cast_type.startswith('(') or cast_type.endswith(')'):
            continue

         # Skip single-char names — likely macro parameters, not types
         if len(cast_type) == 1 and cast_type.islower():
            continue

         # Skip if the cast type is not a known type keyword and
         # doesn't look like a type (no pointer star, no multi-word)
         has_star = '*' in cast_type
         is_type_kw = cast_type.rstrip(' *') in _TYPE_KW
         has_struct = cast_type.startswith(('struct ', 'enum ',
                                           'union ', 'const '))

         if not has_star and not is_type_kw and not has_struct:
            # Single identifier — could be a type alias or a
            # macro parameter. Only flag if it starts with uppercase
            # or contains underscore (type naming convention).
            base = cast_type.rstrip(' *')
            if base.islower() and '_' not in base:
               continue

         # Skip preprocessor lines (#define macros)
         line_text_raw = ctx.lines[
            _tokens_mod.offset_to_line_col(masked, start)[0] - 1
         ] if start < len(masked) else ''
         if line_text_raw.lstrip().startswith('#'):
            continue

         # Check for compound literal: (Type){
         after_close = paren_content[close_idx + 1:].lstrip()
         if after_close.startswith('{'):
            continue

         line, col = _tokens_mod.offset_to_line_col(masked, start)
         line_text = ctx.lines[line - 1] \
            if line - 1 < len(ctx.lines) else ''

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=line,
            col=col,
            end_line=line,
            end_col=col + len(m.group(0)),
            rule=self.id,
            severity=self.severity,
            message='prefer static_cast<T>(x) over C-style (T)x',
            snippet=line_text.strip(),
            suggestion='prefer static_cast<T>(x) over C-style (T)x',
            is_gradient=True,
            prettiness_cost=COST_MILD,
         ))

      return out


RULE = PreferCppCast()
