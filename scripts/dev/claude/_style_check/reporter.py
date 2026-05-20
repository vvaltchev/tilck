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

      out.write("{}:{}:{}: [{}] {} -- {}\n".format(
         d.file,
         d.line,
         d.col,
         d.severity,
         d.rule,
         d.message,
      ))

      if d.snippet:
         out.write("    {}\n".format(d.snippet))

   out.flush()


def emit(diags: List[Diagnostic], fmt: str = 'jsonl', stream=None) -> None:

   if fmt == 'jsonl':
      emit_jsonl(diags, stream)
   elif fmt == 'text':
      emit_text(diags, stream)
   else:
      raise ValueError("unknown format: {}".format(fmt))
