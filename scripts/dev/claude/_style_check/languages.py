# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=missing-function-docstring, bad-indentation
"""
The language of a source file, which decides the rules that apply to it.

C and C++ go by their suffix. Python is a `.py` file, or a script without
a suffix whose shebang runs python (the test runners, the dev scripts).
"""

import re

from pathlib import Path
from typing import Optional

C = 'c'
PYTHON = 'python'

C_SUFFIXES = ('.c', '.h', '.cpp', '.hpp')

_PY_SHEBANG_RE = re.compile(rb'^#!.*\bpython[0-9.]*\b')


def language_of(path: Path) -> Optional[str]:

   if path.suffix in C_SUFFIXES:
      return C

   if path.suffix == '.py':
      return PYTHON

   if path.suffix:
      return None

   try:
      with open(path, 'rb') as fh:
         first = fh.readline(256)
   except OSError:
      return None

   return PYTHON if _PY_SHEBANG_RE.match(first) else None
