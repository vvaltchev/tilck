# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=missing-module-docstring, missing-function-docstring
# pylint: disable=bad-indentation, consider-using-f-string

import os
import json
import sys

from dataclasses import asdict
from typing import List

from .rules.base import Diagnostic


# ANSI escape codes. Kept narrow on purpose -- we only want enough
# distinct hues to give each piece of a diagnostic its own visual
# slot (location, severity, rule, message, snippet, suggestion).
_RESET   = '\x1b[0m'
_BOLD    = '\x1b[1m'
_DIM     = '\x1b[2m'
_RED     = '\x1b[31m'
_GREEN   = '\x1b[32m'
_YELLOW  = '\x1b[33m'
_BLUE    = '\x1b[34m'
_MAGENTA = '\x1b[35m'
_CYAN    = '\x1b[36m'


def _decide_color(mode: str, stream) -> bool:
   """Resolve --color={auto,always,never} to a boolean. `auto` is
   on when stdout is a TTY and NO_COLOR is not set in the
   environment (https://no-color.org convention)."""

   if mode == 'always':
      return True

   if mode == 'never':
      return False

   if 'NO_COLOR' in os.environ:
      return False

   return getattr(stream, 'isatty', lambda: False)()


def _wrap_factory(use_color: bool):
   """Return a `wrap(code, s) -> str` helper. With color off, it is
   a pass-through; with color on, it wraps `s` with `code` and a
   reset. Centralising this keeps the formatter readable."""

   if not use_color:
      return lambda code, s: s

   return lambda code, s: '{}{}{}'.format(code, s, _RESET)


def emit_jsonl(diags: List[Diagnostic], stream=None) -> None:
   """Machine-readable output. Never colored regardless of TTY."""

   out = stream if stream is not None else sys.stdout

   for d in diags:
      out.write(json.dumps(asdict(d)) + '\n')

   out.flush()


def emit_text(diags: List[Diagnostic],
              stream=None,
              color_mode: str = 'auto',
              with_total: bool = True) -> None:
   """Human-readable output. Each diagnostic occupies its own
   block separated by a blank line:

       <file>:<line>:<col>:  error  rule_id  (score: -10.0)
          message text on its own line, wrapped if long
       | <snippet from the source line>
       suggest: <optional suggestion>
   """

   out = stream if stream is not None else sys.stdout
   use_color = _decide_color(color_mode, out)
   wrap = _wrap_factory(use_color)

   for i, d in enumerate(diags):

      # Blank line between diagnostics (not before the first one).
      if i > 0:
         out.write('\n')

      severity_code = _RED if d.severity == 'error' else _YELLOW
      severity_text = wrap(_BOLD + severity_code, d.severity)

      location = wrap(_CYAN, '{}:{}:{}:'.format(d.file, d.line, d.col))
      rule_text = wrap(_MAGENTA, d.rule)

      score_part = ''

      if d.score != 0.0:
         score_part = wrap(
            _DIM, '  (score: {:+.1f})'.format(d.score)
         )

      out.write('{} {}  {}{}\n'.format(
         location, severity_text, rule_text, score_part
      ))

      out.write('   {}\n'.format(d.message))

      if d.snippet:
         out.write('   {} {}\n'.format(
            wrap(_DIM, '|'),
            wrap(_DIM, d.snippet),
         ))

      if d.suggestion:
         out.write('   {} {}\n'.format(
            wrap(_GREEN, 'suggest:'),
            d.suggestion,
         ))

   if with_total:

      total = sum(d.score for d in diags)

      if total != 0.0 and len(diags) > 1:
         out.write('\n')
         out.write(wrap(
            _BOLD,
            'total prettiness: {:+.1f} across {} diagnostic(s)\n'.format(
               total, len(diags)
            )
         ))

   out.flush()


def emit(diags: List[Diagnostic],
         fmt: str = 'jsonl',
         stream=None,
         color_mode: str = 'auto',
         with_total: bool = True) -> None:

   if fmt == 'jsonl':
      emit_jsonl(diags, stream)
   elif fmt == 'text':
      emit_text(
         diags, stream, color_mode=color_mode, with_total=with_total
      )
   else:
      raise ValueError("unknown format: {}".format(fmt))


# Per-function summary block ----------------------------------------

_VERDICT_COLOR = {
   'clean':  _GREEN,
   'drift':  _YELLOW,
   'ugly':   _YELLOW,
   'broken': _RED,
}

_VERDICT_TAG = {
   'clean':  ' CLEAN',
   'drift':  ' DRIFT',
   'ugly':   '  UGLY',
   'broken': 'BROKEN',
}


def emit_function_summaries(file_summary,
                            stream=None,
                            color_mode: str = 'auto',
                            per_statement: bool = True) -> None:
   """Render a per-function table beneath a file's diagnostics:

       function_name (lines A..B) [VERDICT]  total=-X.X  norm=-X.X
                                              hard=N    soft=N

   With per_statement=True (default) and when a function has
   diagnostics clustered on the same line, the worst lines (top 3
   by absolute score) are listed beneath the function:

         line N  ([R1, R2, ...]) total -X.X
         ...

   Only functions with at least one diagnostic are shown. The
   colored verdict tag mirrors the file-level [ OK / WARN / FAIL ]
   labels for instant visual grouping."""

   out = stream if stream is not None else sys.stdout
   use_color = _decide_color(color_mode, out)
   wrap = _wrap_factory(use_color)

   touched = [f for f in file_summary.functions if f.diagnostics]

   if not touched:
      return

   out.write('\n')
   header = '  per-function prettiness summary:'
   out.write(wrap(_BOLD, header) + '\n')

   for f in touched:

      tag = _VERDICT_TAG[f.verdict]
      tag_colored = wrap(_BOLD + _VERDICT_COLOR[f.verdict], tag)

      lhs = '    {} (lines {}..{})'.format(
         f.name, f.start_line, f.end_line
      )
      rhs = ('total {:+.1f}  norm {:+.2f}  hard {}  soft {}').format(
         f.total_score, f.normalized_score,
         f.hard_violations, f.soft_violations
      )

      out.write('{}  [{}]  {}\n'.format(lhs, tag_colored, rhs))

      if per_statement and len(f.diagnostics) > 1:
         _emit_worst_lines(f, out, wrap)

   if file_summary.file_level_diagnostics:

      flv = file_summary.file_level_diagnostics
      total = sum(d.score for d in flv)
      out.write('    {} file-level diagnostic(s) total {:+.1f}\n'.format(
         len(flv), total
      ))

   out.flush()


def _emit_worst_lines(func, out, wrap) -> None:
   """Group `func.diagnostics` by source line and list the top-3
   lines by absolute score. Lines with multiple diagnostics are
   the interesting per-statement clusters; surfacing them lets
   the reader find the densest problem spots quickly."""

   by_line = {}
   for d in func.diagnostics:
      slot = by_line.setdefault(d.line, [])
      slot.append(d)

   # Skip the breakdown if no line has multiple diags (the per-
   # diagnostic listing already shows the same info).
   if not any(len(v) > 1 for v in by_line.values()):
      return

   line_totals = [
      (line, sum(d.score for d in diags), diags)
      for line, diags in by_line.items()
   ]
   line_totals.sort(key=lambda t: t[1])   # most negative first

   worst = line_totals[:3]

   for line, total, diags in worst:

      if total >= 0:
         break

      ids = sorted({d.rule for d in diags})
      rule_list = ', '.join(ids)
      out.write(
         '         line {:>4}  total {:+.1f}  ({})\n'.format(
            line, total, rule_list
         )
      )
