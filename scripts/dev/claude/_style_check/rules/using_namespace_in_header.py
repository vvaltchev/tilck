# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring

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

# `using namespace <name>;` at any indentation level.
_USING_NS_PAT = re.compile(r'^\s*using\s+namespace\s+(\w+)')


class UsingNamespaceInHeader(Rule):

   id = 'using_namespace_in_header'
   description = (
      '`using namespace` in a header pollutes every includer\'s namespace'
   )
   layers = LAYER_RAW_TEXT
   severity = SEVERITY_WARNING
   default_score = SCORE_STRONG_PREF
   applies_to = {'.h', '.hpp'}

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      if not ctx.is_header:
         return []

      out = []

      for lineno, line_text in enumerate(ctx.lines, start=1):

         m = _USING_NS_PAT.match(line_text)

         if not m:
            continue

         ns_name = m.group(1)

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=lineno,
            col=m.start() + 1,
            end_line=lineno,
            end_col=m.end() + 1,
            rule=self.id,
            severity=self.severity,
            message=(
               '`using namespace {}` in a header pollutes '
               'every includer\'s namespace'
            ).format(ns_name),
            snippet=line_text.strip(),
         ))

      return out


RULE = UsingNamespaceInHeader()
