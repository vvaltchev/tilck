# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=consider-using-f-string

import re

from typing import List

from .base import (
   Rule,
   Diagnostic,
   CheckContext,
   LAYER_RAW_TEXT,
   SEVERITY_WARNING,
   SCORE_STRONG_PREF,
)

# Q28: multi-statement function-like macros must use
# `do { ... } while (0)` wrap. The `{ ... }`-only form (V2)
# is strongly dispreferred -- it lets the caller omit `;`,
# producing call sites that look inconsistent with normal
# statements. Detection: a multi-line `#define` whose final
# logical line ends with `}` and NOT `} while (0)` (and was
# not just a single-expression macro).
#
# Exceptions -- brace-only is the correct form when:
#
#   1. Declaration macros: body contains `static`, `struct X =`,
#      `extern`, or designated-initializer `.field =` at the
#      outermost level. These expand to file-scope declarations,
#      not executable statements.
#
#   2. Flow-control macros: body contains `continue`, `return`,
#      or computed `goto` (`goto *`). Wrapping in `do { } while (0)`
#      would change the semantics of `continue` (it restarts the
#      wrapper, not the enclosing loop) and `return` is fine
#      either way but these macros are intentionally designed to
#      be used inside specific loop constructs (e.g. norec.h).
#
#   3. Conditional-guard macros: body is `if (cond) { stmt; }`
#      with no else -- a single guarded call, not multi-statement.
#      Wrapping these in `do { } while (0)` is unnecessary and
#      adds noise; the `if` already provides the scoping.
#
#   4. Function-definition macros: body defines one or more
#      static/inline function definitions (e.g. atomics.h
#      _DEFINE_ATOMIC_INT_OPS).

_DEFINE_START = re.compile(r'^\s*#\s*define\s+\w+(?:\([^)]*\))?')

# Patterns that indicate the macro body is declarations, not
# executable statements.
_DECL_INDICATORS = re.compile(
   r'\bstatic\b|\bextern\b|\bstruct\s+\w+\s+\w+\s*='
   r'|\.\w+\s*='       # designated initializer
   r'|\bSTATIC_ASSERT\b'
   r'|\bstruct\s+\w+\s*\{'   # struct definition
   r'|\bvoid\s+[\w#]+\s*\('   # function definition (## paste ok)
)

# Flow-control keywords that break under do { } while (0).
_FLOW_CONTROL = re.compile(
   r'\bcontinue\b|\breturn\b|\bgoto\s*\*'
)

# Pure initializer: body is just `{ expr, expr, ... }` with no
# semicolons inside (used as aggregate initializer, not statement).
_PURE_INITIALIZER = re.compile(
   r'^\{[^;]*\}$'
)

# Conditional-guard: body is `if (...) { ... }` with no else.
_IF_GUARD = re.compile(
   r'^\s*#\s*define\s+\w+\([^)]*\)\s*'
   r'if\s*\('
)


class MacroBraceOnlyForbidden(Rule):

   id = 'macro_brace_only_forbidden'
   description = (
      'Multi-statement function-like macros should be wrapped in '
      '`do { ... } while (0)`, not just `{ ... }`. The brace-only '
      'form lets the caller omit the trailing `;`. (Q28 strong '
      'preference.)'
   )
   layers = LAYER_RAW_TEXT
   severity = SEVERITY_WARNING
   default_score = SCORE_STRONG_PREF

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      out = []
      i = 0
      lines = ctx.lines

      while i < len(lines):

         line = lines[i]

         if not _DEFINE_START.match(line):
            i += 1
            continue

         # Multi-line macro: line ends with backslash (after any
         # whitespace). Collect lines until a non-continuation line.
         if not line.rstrip().endswith('\\'):
            i += 1
            continue

         macro_start = i
         j = i

         while j < len(lines) and lines[j].rstrip().endswith('\\'):
            j += 1

         # j now points at the last (non-continuation) line of the
         # macro definition.
         if j >= len(lines):
            i = j
            continue

         last = lines[j].rstrip()
         macro_body_lines = [lines[k] for k in range(macro_start, j + 1)]

         # Look at the LAST non-blank content past the last `\`
         # continuation. We want to know if the macro ends with `}`
         # alone (V2) or `} while (0)` / `} while(0)` (V1).
         # Concatenate body, strip backslashes.
         joined = ' '.join(s.rstrip('\\').strip()
                           for s in macro_body_lines)
         joined = re.sub(r'\s+', ' ', joined).strip()

         if not joined.endswith('}'):
            i = j + 1
            continue

         # Does the body contain `do {` somewhere? If yes, this is
         # likely a `do { ... } while (...)` body -- not the V2 form.
         if 'do {' in joined or 'do{' in joined:
            i = j + 1
            continue

         # Strip the #define header to get just the body.
         body = re.sub(
            r'^\s*#\s*define\s+\w+(?:\([^)]*\))?\s*', '', joined
         )

         # Exception 1+4: declaration / function-definition macros.
         if _DECL_INDICATORS.search(body):
            i = j + 1
            continue

         # Exception 2: flow-control macros.
         if _FLOW_CONTROL.search(body):
            i = j + 1
            continue

         # Exception 2b: pure initializer (no semicolons inside).
         if _PURE_INITIALIZER.match(body.strip()):
            i = j + 1
            continue

         # Exception 3: conditional-guard macros (`if (...) { }`)
         if body.lstrip().startswith('if') and 'else' not in body:
            i = j + 1
            continue

         # V2 shape: brace-only multi-line macro.
         col = lines[macro_start].find('#') + 1
         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=macro_start + 1,
            col=col if col > 0 else 1,
            end_line=j + 1,
            end_col=len(last) + 1,
            rule=self.id,
            severity=self.severity,
            message=(
               'multi-statement macro uses `{ ... }` wrap -- prefer '
               '`do { ... } while (0)` so callers must include `;` '
               'at the use site'
            ),
            snippet=lines[macro_start].strip(),
            suggestion='wrap the body in `do { ... } while (0)`',
         ))

         i = j + 1

      return out


RULE = MacroBraceOnlyForbidden()
