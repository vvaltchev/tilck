# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=consider-using-f-string

import re

from typing import List

from .base import (
   COST_MINOR,
   Rule,
   Diagnostic,
   CheckContext,
   LAYER_RAW_TEXT,
   SEVERITY_WARNING,
   SCORE_NUDGE,
)

# Match preprocessor conditional directives.
_PP_COND = re.compile(
   r'^\s*#\s*(ifdef|ifndef|if|else|elif)\b'
)

# Match `#if 0` / `#if 1` (simple comment/toggle blocks -- skip).
_IF_01 = re.compile(r'^\s*#\s*if\s+[01]\s*$')

# Match header-guard `#ifndef SYMBOL` pattern.
_IFNDEF_GUARD = re.compile(r'^\s*#\s*ifndef\s+\w+')


class IfdefDensity(Rule):

   id = 'ifdef_density'
   description = (
      'Each preprocessor conditional (#ifdef, #ifndef, #if, #else, '
      '#elif) adds visual noise and complexity. Gradient rule: each '
      'directive reduces per-line prettiness by COST_MINOR.'
   )
   layers = LAYER_RAW_TEXT
   severity = SEVERITY_WARNING
   default_score = SCORE_NUDGE

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      out = []

      for i, line in enumerate(ctx.lines):

         line_no = i + 1

         if not _PP_COND.match(line):
            continue

         # Skip `#if 0` / `#if 1` (simple comment blocks)
         if _IF_01.match(line):
            continue

         # Skip header-guard #ifndef in the first 5 lines
         if ctx.is_header and line_no <= 5 and _IFNDEF_GUARD.match(line):
            continue

         stripped = line.strip()

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=line_no,
            col=1,
            end_line=line_no,
            end_col=len(line) + 1,
            rule=self.id,
            severity=self.severity,
            message=(
               'preprocessor conditional adds visual noise: %s'
               % stripped
            ),
            snippet=stripped,
            is_gradient=True,
            prettiness_cost=COST_MINOR,
         ))

      return out


RULE = IfdefDensity()
