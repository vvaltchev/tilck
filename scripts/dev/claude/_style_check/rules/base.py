# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=missing-module-docstring, missing-class-docstring
# pylint: disable=too-few-public-methods, missing-function-docstring

from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional

# Severity levels
SEVERITY_ERROR = 'error'
SEVERITY_WARNING = 'warning'

# Layer tags
LAYER_RAW_TEXT = 'R'
LAYER_TOKENS = 'T'
LAYER_STRUCTURAL = 'S'
LAYER_COMMENTS = 'C'


@dataclass
class Diagnostic:
   file: str
   line: int
   col: int
   end_line: int
   end_col: int
   rule: str
   severity: str
   message: str
   snippet: Optional[str] = None


@dataclass
class CheckContext:

   # Always available
   file_path: Path
   source_bytes: bytes
   source_text: str
   lines: List[str]
   is_header: bool
   is_cpp: bool

   # libclang artefacts -- may be None if parse failed or rule didn't need them
   tu: Optional[object] = None
   extents: list = field(default_factory=list)

   # Comment ranges from the raw-text scanner: list of CommentRange tuples
   comments: list = field(default_factory=list)


class Rule:

   # Subclasses override these
   id: str = ''
   description: str = ''
   layers: str = ''
   severity: str = SEVERITY_ERROR

   # Capability requirements
   needs_tu: bool = False
   needs_comments: bool = False

   # File-type filter: set of file extensions this rule applies to.
   # None means: all C source files (.c and .h). The tool does NOT
   # check .cpp/.hpp in v1.
   applies_to: Optional[set] = None  # e.g. {'.c'} or {'.h'}

   def applies_to_file(self, path: Path) -> bool:
      if self.applies_to is None:
         return path.suffix in ('.c', '.h')
      return path.suffix in self.applies_to

   def check(self, ctx: CheckContext) -> List[Diagnostic]:
      raise NotImplementedError(self.id)

   def explain(self) -> str:
      return self.description
