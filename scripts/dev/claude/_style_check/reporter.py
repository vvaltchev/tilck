# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=missing-module-docstring, missing-function-docstring

import json
import sys

from dataclasses import asdict
from typing import List

from .rules.base import Diagnostic


def emit_jsonl(diags: List[Diagnostic], stream=None) -> None:

   out = stream if stream is not None else sys.stdout

   for d in diags:
      out.write(json.dumps(asdict(d)) + '\n')

   out.flush()


def emit_text(diags: List[Diagnostic], stream=None) -> None:

   out = stream if stream is not None else sys.stdout

   for d in diags:

      score_part = ""

      if d.score != 0.0:
         score_part = "  (score: {:+.1f})".format(d.score)

      out.write("{}:{}:{}: [{}] {} -- {}{}\n".format(
         d.file,
         d.line,
         d.col,
         d.severity,
         d.rule,
         d.message,
         score_part,
      ))

      if d.snippet:
         out.write("    {}\n".format(d.snippet))

      if d.suggestion:
         out.write("    suggest: {}\n".format(d.suggestion))

   total = sum(d.score for d in diags)

   if total != 0.0 and len(diags) > 1:
      out.write("\n  total prettiness: {:+.1f} across {} diagnostic(s)\n".format(
         total, len(diags)
      ))

   out.flush()


def emit(diags: List[Diagnostic], fmt: str = 'jsonl', stream=None) -> None:

   if fmt == 'jsonl':
      emit_jsonl(diags, stream)
   elif fmt == 'text':
      emit_text(diags, stream)
   else:
      raise ValueError("unknown format: {}".format(fmt))
